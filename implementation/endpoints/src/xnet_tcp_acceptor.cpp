#include <algorithm>
#include <cstring>
#include <limits>
#include <thread>

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/endian/conversion.hpp>

#if !defined(VSOMEIP_ENABLE_XNET)
#include <sys/types.h>
#include <sys/socket.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#endif
#endif

#include "../include/xnet_tcp_acceptor.hpp"
#include "../include/backend_socket_option_helpers.hpp"
#include "../include/xnet_tcp_socket.hpp"
#include "../include/xnet_error.hpp"
#include "../include/xnet_api.hpp"

#include "logger_ext.hpp"

#define VSOMEIP_LOG_PREFIX "xna"

#if defined(VSOMEIP_ENABLE_XNET)
#define INVALID_SOCKET_VALUE nxINVALID_SOCKET
#else
#if defined(_WIN32)
#define INVALID_SOCKET_VALUE static_cast<nxSOCKET>(INVALID_SOCKET)
#else
#define INVALID_SOCKET_VALUE static_cast<nxSOCKET>(-1)
#endif
#endif
#define SOCKET_ERROR_VALUE -1

namespace vsomeip_v3 {

namespace {

boost::system::error_code make_xnet_error(char const* _operation) {
    const auto its_raw_error = xnet_get_last_error();
    auto its_mapped_error = xnet_to_boost_error(its_raw_error);
    if (its_mapped_error != boost::asio::error::would_block &&
        its_mapped_error != boost::asio::error::try_again &&
        its_mapped_error != boost::asio::error::in_progress) {
        VSOMEIP_ERROR << "[XNET][tcp_acceptor][" << _operation << "] failed"
                      << " raw_error=" << its_raw_error
                      << " mapped_error=" << its_mapped_error.value()
                      << " message=" << its_mapped_error.message();
    }
    return its_mapped_error;
}

void post_completion(boost::asio::io_context& _io, tcp_base_socket::connect_handler _handler, boost::system::error_code const& _ec) {
    auto its_executor = boost::asio::get_associated_executor(_handler, _io.get_executor());
    boost::asio::post(its_executor, [handler = std::move(_handler), ec = _ec]() mutable { handler(ec); });
}

bool endpoint_to_native(boost::asio::ip::tcp::endpoint const& _endpoint, nxsockaddr_storage& _storage, nxsocklen_t& _len,
                        boost::system::error_code& _ec) {
    std::memset(&_storage, 0, sizeof(_storage));
    if (_endpoint.address().is_v4()) {
        auto* its_addr = reinterpret_cast<nxsockaddr_in*>(&_storage);
        its_addr->sin_family = nxAF_INET;
        its_addr->sin_port = boost::endian::native_to_big(_endpoint.port());
        its_addr->sin_addr.addr = boost::endian::native_to_big(_endpoint.address().to_v4().to_uint());
        _len = static_cast<nxsocklen_t>(sizeof(nxsockaddr_in));
        _ec.clear();
        return true;
    }

    if (_endpoint.address().is_v6()) {
        auto* its_addr = reinterpret_cast<nxsockaddr_in6*>(&_storage);
        its_addr->sin6_family = nxAF_INET6;
        its_addr->sin6_port = boost::endian::native_to_big(_endpoint.port());
        its_addr->sin6_flowinfo = 0;
        its_addr->sin6_scope_id = _endpoint.address().to_v6().scope_id();
        const auto its_bytes = _endpoint.address().to_v6().to_bytes();
        std::memcpy(its_addr->sin6_addr.addr, its_bytes.data(), its_bytes.size());
        _len = static_cast<nxsocklen_t>(sizeof(nxsockaddr_in6));
        _ec.clear();
        return true;
    }

    _ec = boost::asio::error::make_error_code(boost::asio::error::address_family_not_supported);
    return false;
}

bool native_to_endpoint(nxsockaddr_storage const& _storage, nxsocklen_t _len, boost::asio::ip::tcp::endpoint& _endpoint,
                        boost::system::error_code& _ec) {
    const auto* its_sockaddr = reinterpret_cast<const nxsockaddr*>(&_storage);
    if (its_sockaddr->sa_family == nxAF_INET && static_cast<std::size_t>(_len) >= sizeof(nxsockaddr_in)) {
        const auto* its_addr = reinterpret_cast<const nxsockaddr_in*>(&_storage);
        const auto its_ip = boost::asio::ip::address_v4(boost::endian::big_to_native(its_addr->sin_addr.addr));
        const auto its_port = boost::endian::big_to_native(its_addr->sin_port);
        _endpoint = boost::asio::ip::tcp::endpoint(its_ip, its_port);
        _ec.clear();
        return true;
    }

    if (its_sockaddr->sa_family == nxAF_INET6 && static_cast<std::size_t>(_len) >= sizeof(nxsockaddr_in6)) {
        const auto* its_addr = reinterpret_cast<const nxsockaddr_in6*>(&_storage);
        boost::asio::ip::address_v6::bytes_type its_bytes{};
        std::memcpy(its_bytes.data(), its_addr->sin6_addr.addr, its_bytes.size());
        const auto its_ip = boost::asio::ip::address_v6(its_bytes, its_addr->sin6_scope_id);
        const auto its_port = boost::endian::big_to_native(its_addr->sin6_port);
        _endpoint = boost::asio::ip::tcp::endpoint(its_ip, its_port);
        _ec.clear();
        return true;
    }

    _ec = boost::asio::error::make_error_code(boost::asio::error::address_family_not_supported);
    return false;
}

bool wait_read_ready(nxSOCKET _socket, std::chrono::milliseconds _timeout,
                     std::atomic<bool> const& _stop_requested,
                     std::atomic<std::uint64_t> const& _cancel_epoch,
                     std::uint64_t _operation_epoch,
                     boost::system::error_code& _ec) {
    auto timeout_ms = _timeout.count();
    if (timeout_ms < 0 || timeout_ms > std::numeric_limits<int>::max()) {
        _ec = boost::asio::error::make_error_code(boost::asio::error::invalid_argument);
        return false;
    }

    const auto timeout_sec = static_cast<long>(timeout_ms / 1000);
    const auto timeout_usec = static_cast<long>((timeout_ms % 1000) * 1000);

    for (;;) {
#if defined(VSOMEIP_ENABLE_XNET)
        nxfd_set read_fds{};
        nxfd_set except_fds{};
        nxFD_ZERO(&read_fds);
        nxFD_ZERO(&except_fds);

        nxFD_SET(_socket, &read_fds);
        nxFD_SET(_socket, &except_fds);

        nxtimeval timeout{};
        timeout.tv_sec = timeout_sec;
        timeout.tv_usec = static_cast<int32_t>(timeout_usec);
        const auto its_result = xnet_api::nxselect(0, &read_fds, nullptr, &except_fds, &timeout);
#else
        fd_set read_fds{};
        fd_set except_fds{};
        FD_ZERO(&read_fds);
        FD_ZERO(&except_fds);

        FD_SET(static_cast<int>(_socket), &read_fds);
        FD_SET(static_cast<int>(_socket), &except_fds);

        timeval timeout{};
        timeout.tv_sec = timeout_sec;
        timeout.tv_usec = timeout_usec;
#if defined(_WIN32)
        const auto its_result = ::select(0, &read_fds, nullptr, &except_fds, &timeout);
#else
        const auto its_result = ::select(static_cast<int>(_socket) + 1, &read_fds, nullptr, &except_fds, &timeout);
#endif
#endif

        if (its_result > 0) {
            _ec.clear();
            return true;
        }

        if (its_result == 0) {
            if (_stop_requested.load(std::memory_order_relaxed)
                || _cancel_epoch.load(std::memory_order_relaxed) != _operation_epoch) {
                _ec = boost::asio::error::operation_aborted;
                return false;
            }
            _ec.clear();
            return false;
        }

        _ec = make_xnet_error("wait_read_ready");
        if (_ec == boost::asio::error::interrupted) {
            continue;
        }
        return false;
    }
}

boost::system::error_code make_unsupported_option_error(char const* _option, char const* _reason) {
    VSOMEIP_WARNING << "[XNET][tcp_acceptor][" << _option << "] unsupported: " << _reason;
    return boost::asio::error::make_error_code(boost::asio::error::operation_not_supported);
}

} // namespace

xnet_tcp_acceptor::xnet_tcp_acceptor(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack) :
    xnet_tcp_acceptor(_io, _xnet_stack, {}) {
}

xnet_tcp_acceptor::xnet_tcp_acceptor(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack, std::shared_ptr<void> _stack_lifetime)
    : acceptor_(INVALID_SOCKET_VALUE),
      io_context_(_io),
      xnet_stack_(_xnet_stack),
      stack_lifetime_(std::move(_stack_lifetime)),
      is_ipv6_(false),
      stop_requested_(false),
      cancel_epoch_(0) {
    VSOMEIP_INFO << "[XNET][tcp_acceptor][ctor] acceptor created";
}

xnet_tcp_acceptor::~xnet_tcp_acceptor() {
    boost::system::error_code ec;
    close(ec);
}

void xnet_tcp_acceptor::ensure_worker_thread() {
    std::lock_guard<std::mutex> its_lock(worker_mutex_);
    if (worker_thread_.joinable()) {
        return;
    }

    stop_requested_.store(false, std::memory_order_relaxed);
    worker_thread_ = std::thread([this]() { worker_loop(); });
}

void xnet_tcp_acceptor::stop_worker_thread() {
    {
        std::lock_guard<std::mutex> its_lock(worker_mutex_);
        stop_requested_.store(true, std::memory_order_relaxed);
    }
    worker_cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    std::lock_guard<std::mutex> its_lock(worker_mutex_);
    work_queue_.clear();
}

bool xnet_tcp_acceptor::enqueue_work(work_item_t&& _item) {
    ensure_worker_thread();
    {
        std::lock_guard<std::mutex> its_lock(worker_mutex_);
        if (stop_requested_.load(std::memory_order_relaxed)) {
            return false;
        }
        work_queue_.emplace_back(std::move(_item));
    }
    worker_cv_.notify_one();
    return true;
}

void xnet_tcp_acceptor::worker_loop() {
    for (;;) {
        work_item_t its_item;
        {
            std::unique_lock<std::mutex> its_lock(worker_mutex_);
            worker_cv_.wait(its_lock, [this]() {
                return stop_requested_.load(std::memory_order_relaxed) || !work_queue_.empty();
            });

            if (stop_requested_.load(std::memory_order_relaxed) && work_queue_.empty()) {
                return;
            }

            its_item = std::move(work_queue_.front());
            work_queue_.pop_front();
        }

        if (its_item) {
            its_item();
        }
    }
}

bool xnet_tcp_acceptor::is_open() const {
    return acceptor_ != INVALID_SOCKET_VALUE;
}

int xnet_tcp_acceptor::native_handle() {
    return static_cast<int>(acceptor_);
}

void xnet_tcp_acceptor::open(boost::asio::ip::tcp::endpoint::protocol_type _pt, boost::system::error_code& _ec) {
    if (is_open()) {
        close(_ec);
        if (_ec) {
            return;
        }
    }

    is_ipv6_ = (_pt == boost::asio::ip::tcp::v6());

#if defined(VSOMEIP_ENABLE_XNET)
    acceptor_ = xnet_api::nxsocket(xnet_stack_, is_ipv6_ ? nxAF_INET6 : nxAF_INET, nxSOCK_STREAM, nxIPPROTO_TCP);
    const bool is_invalid_socket = (acceptor_ == nxINVALID_SOCKET);
#else
    acceptor_ = ::socket(is_ipv6_ ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
#if defined(_WIN32)
    const bool is_invalid_socket = (acceptor_ == static_cast<nxSOCKET>(INVALID_SOCKET));
#else
    const bool is_invalid_socket = (acceptor_ == INVALID_SOCKET_VALUE);
#endif
#endif

    if (is_invalid_socket) {
        acceptor_ = INVALID_SOCKET_VALUE;
        _ec = make_xnet_error("open");
        return;
    }

    _ec.clear();
}

bool xnet_tcp_acceptor::wait_for_pending_connection(std::chrono::milliseconds _timeout, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return false;
    }

    return wait_read_ready(acceptor_, _timeout, stop_requested_, cancel_epoch_, cancel_epoch_.load(std::memory_order_relaxed), _ec);
}

void xnet_tcp_acceptor::bind(boost::asio::ip::tcp::endpoint const& _ep, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    nxsockaddr_storage its_storage{};
    nxsocklen_t its_len = 0;
    if (!endpoint_to_native(_ep, its_storage, its_len, _ec)) {
        return;
    }

#if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxbind(acceptor_, reinterpret_cast<nxsockaddr*>(&its_storage), its_len) == SOCKET_ERROR_VALUE) {
#else
    if (::bind(static_cast<int>(acceptor_), reinterpret_cast<sockaddr*>(&its_storage), static_cast<socklen_t>(its_len))
        == SOCKET_ERROR_VALUE) {
#endif
        _ec = make_xnet_error("bind");
        return;
    }

    _ec.clear();
}

void xnet_tcp_acceptor::close(boost::system::error_code& _ec) {
    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);

    // Guarantee that the worker thread is always stopped and joined, even if
    // the backend close call below fails and the function returns early. A
    // joinable std::thread destroyed during acceptor teardown would otherwise
    // call std::terminate().
    struct worker_stop_guard {
        xnet_tcp_acceptor* self;
        ~worker_stop_guard() { self->stop_worker_thread(); }
    } its_worker_stop_guard{this};

    if (!is_open()) {
        _ec.clear();
        return;
    }

#if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxclose(acceptor_) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("close");
        return;
    }
#else
#if defined(_WIN32)
    if (::closesocket(static_cast<SOCKET>(acceptor_)) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("close");
        return;
    }
#else
    if (::close(static_cast<int>(acceptor_)) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("close");
        return;
    }
#endif
#endif

    acceptor_ = INVALID_SOCKET_VALUE;
    _ec.clear();
}

void xnet_tcp_acceptor::cancel(boost::system::error_code& _ec) {
    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);
    worker_cv_.notify_all();
    _ec.clear();
}

void xnet_tcp_acceptor::listen(int _backlog, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    const auto its_backlog = std::min(_backlog, static_cast<int>(std::numeric_limits<int16_t>::max()));
#if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxlisten(acceptor_, its_backlog) == SOCKET_ERROR_VALUE) {
#else
    if (::listen(static_cast<int>(acceptor_), its_backlog) == SOCKET_ERROR_VALUE) {
#endif
        _ec = make_xnet_error("listen");
        return;
    }

    _ec.clear();
}

void xnet_tcp_acceptor::set_option(boost::asio::ip::tcp::socket::reuse_address _ra, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    int opt = _ra.value() ? 1 : 0;
#if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxsetsockopt(acceptor_, nxSOL_SOCKET, nxSO_REUSEADDR, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
#else
    if (::setsockopt(static_cast<int>(acceptor_), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt),
                     static_cast<socklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
#endif
        _ec = make_xnet_error("set_option:reuse_address");
        return;
    }

    _ec.clear();
}

#if defined(__linux__)
bool xnet_tcp_acceptor::set_reuse_port() {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

#if defined(VSOMEIP_ENABLE_XNET)
    (void)make_unsupported_option_error("set_reuse_port", "SO_REUSEPORT is not supported by XNET API");
    errno = ENOTSUP;
    return false;
#else
    return socket_option_helpers::set_tcp_acceptor_reuse_port(static_cast<int>(acceptor_));
#endif
}

bool xnet_tcp_acceptor::set_native_option_free_bind() {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

#if defined(VSOMEIP_ENABLE_XNET)
    (void)make_unsupported_option_error("set_native_option_free_bind", "IP_FREEBIND is not supported by XNET API");
    errno = ENOTSUP;
    return false;
#else
    return socket_option_helpers::set_tcp_acceptor_free_bind(static_cast<int>(acceptor_));
#endif
}
#endif

#if defined(__linux__) || defined(__QNX__)
bool xnet_tcp_acceptor::bind_to_device(std::string const& _device) {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

#if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxsetsockopt(acceptor_, nxSOL_SOCKET, nxSO_BINDTODEVICE, _device.c_str(), static_cast<nxsocklen_t>(_device.size()))
        == SOCKET_ERROR_VALUE) {
        (void)make_xnet_error("bind_to_device");
        return false;
    }
    return true;
#else
    return socket_option_helpers::set_bind_to_device(static_cast<int>(acceptor_), _device);
#endif
}
#endif

void xnet_tcp_acceptor::async_accept(tcp_socket& _socket, boost::asio::ip::tcp::endpoint& _peer_ep, connect_handler _handler) {
    if (!is_open()) {
        post_completion(io_context_, std::move(_handler), boost::asio::error::bad_descriptor);
        return;
    }

    auto* its_socket = dynamic_cast<xnet_tcp_socket*>(&_socket);
    if (!its_socket) {
        post_completion(io_context_, std::move(_handler), boost::asio::error::invalid_argument);
        return;
    }

    const auto its_acceptor = acceptor_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto* const its_io = &io_context_;

    if (!enqueue_work([this, its_acceptor, its_epoch, its_socket, peer_ep = std::ref(_peer_ep), handler = std::move(_handler), its_io]() mutable {
        boost::system::error_code its_error;
        nxsockaddr_storage its_peer_storage{};
        nxsocklen_t its_peer_len = static_cast<nxsocklen_t>(sizeof(its_peer_storage));
        nxSOCKET its_client_socket = INVALID_SOCKET_VALUE;

        for (;;) {
            if (is_canceled(its_epoch)) {
                its_error = boost::asio::error::operation_aborted;
                break;
            }

            boost::system::error_code its_wait_error;
            if (!wait_read_ready(its_acceptor, std::chrono::milliseconds(200),
                                 stop_requested_, cancel_epoch_, its_epoch, its_wait_error)) {
                if (its_wait_error) {
                    its_error = its_wait_error;
                    break;
                }
                continue;
            }

#if defined(VSOMEIP_ENABLE_XNET)
            its_client_socket = xnet_api::nxaccept(its_acceptor, reinterpret_cast<nxsockaddr*>(&its_peer_storage), &its_peer_len);
#else
            its_client_socket = ::accept(static_cast<int>(its_acceptor), reinterpret_cast<sockaddr*>(&its_peer_storage),
                                         reinterpret_cast<socklen_t*>(&its_peer_len));
#endif

#if defined(VSOMEIP_ENABLE_XNET)
            const bool is_accept_failed = (its_client_socket == nxINVALID_SOCKET);
#else
#if defined(_WIN32)
            const bool is_accept_failed = (its_client_socket == static_cast<nxSOCKET>(INVALID_SOCKET));
#else
            const bool is_accept_failed = (its_client_socket == INVALID_SOCKET_VALUE);
#endif
#endif

            if (is_accept_failed) {
                its_error = make_xnet_error("async_accept");
                if (its_error == boost::asio::error::would_block || its_error == boost::asio::error::try_again
                    || its_error == boost::asio::error::interrupted || its_error == boost::asio::error::in_progress) {
                    continue;
                }
                break;
            }

            if (!native_to_endpoint(its_peer_storage, its_peer_len, peer_ep.get(), its_error)) {
#if defined(VSOMEIP_ENABLE_XNET)
                (void)xnet_api::nxclose(its_client_socket);
#else
#if defined(_WIN32)
                (void)::closesocket(static_cast<SOCKET>(its_client_socket));
#else
                (void)::close(static_cast<int>(its_client_socket));
#endif
#endif
                its_client_socket = INVALID_SOCKET_VALUE;
                break;
            }

            its_socket->assign_accepted_socket(its_client_socket, peer_ep.get().protocol() == boost::asio::ip::tcp::v6(), its_error);
            if (its_error) {
#if defined(VSOMEIP_ENABLE_XNET)
                (void)xnet_api::nxclose(its_client_socket);
#else
#if defined(_WIN32)
                (void)::closesocket(static_cast<SOCKET>(its_client_socket));
#else
                (void)::close(static_cast<int>(its_client_socket));
#endif
#endif
                its_client_socket = INVALID_SOCKET_VALUE;
            }
            break;
        }

        if (is_canceled(its_epoch) && its_error != boost::asio::error::operation_aborted) {
            its_error = boost::asio::error::operation_aborted;
        }

        post_completion(*its_io, std::move(handler), its_error);
    })) {
        post_completion(io_context_, std::move(_handler), boost::asio::error::operation_aborted);
    }
}

bool xnet_tcp_acceptor::is_canceled(std::uint64_t _epoch) const {
    return cancel_epoch_.load(std::memory_order_relaxed) != _epoch;
}

} // namespace vsomeip_v3

