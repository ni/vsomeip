#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <thread>

#include <boost/asio/associated_executor.hpp>
#include <boost/asio/buffer.hpp>
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

#include "../include/xnet_tcp_socket.hpp"
#include "../include/backend_socket_option_helpers.hpp"
#include "../include/xnet_error.hpp"
#include "../include/xnet_api.hpp"

#include "logger_ext.hpp"

#define VSOMEIP_LOG_PREFIX "xnt"

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

constexpr int SELECT_POLL_TIMEOUT_MS = 200;

inline std::size_t clamp_size_to_int32(std::size_t _size) {
    return std::min(_size, static_cast<std::size_t>(std::numeric_limits<int32_t>::max()));
}

inline bool is_would_block_like(boost::system::error_code const& _ec) {
    return _ec == boost::asio::error::would_block || _ec == boost::asio::error::try_again || _ec == boost::asio::error::in_progress;
}

boost::system::error_code make_xnet_error(char const* _operation) {
    const auto its_raw_error = xnet_get_last_error();
    auto its_mapped_error = xnet_to_boost_error(its_raw_error);
    if (its_mapped_error != boost::asio::error::would_block &&
        its_mapped_error != boost::asio::error::try_again &&
        its_mapped_error != boost::asio::error::in_progress &&
        its_mapped_error != boost::asio::error::bad_descriptor &&
        its_mapped_error != boost::asio::error::operation_aborted) {
        VSOMEIP_ERROR << "[XNET][tcp][" << _operation << "] failed"
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

void post_rw_completion(boost::asio::io_context& _io, tcp_socket::rw_handler _handler, boost::system::error_code const& _ec,
                        std::size_t _bytes) {
    auto its_executor = boost::asio::get_associated_executor(_handler, _io.get_executor());
    boost::asio::post(its_executor, [handler = std::move(_handler), ec = _ec, bytes = _bytes]() mutable { handler(ec, bytes); });
}

bool wait_socket_ready(nxSOCKET _socket, boost::asio::ip::tcp::socket::wait_type _wait,
                       std::atomic<bool> const& _stop_requested,
                       std::atomic<std::uint64_t> const& _cancel_epoch,
                       std::uint64_t _operation_epoch,
                       boost::system::error_code& _ec) {
    for (;;) {
#if defined(VSOMEIP_ENABLE_XNET)
        nxfd_set read_fds{};
        nxfd_set write_fds{};
        nxfd_set except_fds{};
        nxFD_ZERO(&read_fds);
        nxFD_ZERO(&write_fds);
        nxFD_ZERO(&except_fds);

        if (_wait == boost::asio::ip::tcp::socket::wait_read) {
            nxFD_SET(_socket, &read_fds);
        } else if (_wait == boost::asio::ip::tcp::socket::wait_write) {
            nxFD_SET(_socket, &write_fds);
        } else {
            nxFD_SET(_socket, &except_fds);
        }

        nxtimeval timeout{};
        timeout.tv_sec = SELECT_POLL_TIMEOUT_MS / 1000;
        timeout.tv_usec = (SELECT_POLL_TIMEOUT_MS % 1000) * 1000;
        const auto its_result = xnet_api::nxselect(static_cast<int32_t>(_socket + 1), &read_fds, &write_fds, &except_fds, &timeout);
#else
        fd_set read_fds{};
        fd_set write_fds{};
        fd_set except_fds{};
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_ZERO(&except_fds);

        if (_wait == boost::asio::ip::tcp::socket::wait_read) {
            FD_SET(static_cast<int>(_socket), &read_fds);
        } else if (_wait == boost::asio::ip::tcp::socket::wait_write) {
            FD_SET(static_cast<int>(_socket), &write_fds);
        } else {
            FD_SET(static_cast<int>(_socket), &except_fds);
        }

        timeval timeout{};
        timeout.tv_sec = SELECT_POLL_TIMEOUT_MS / 1000;
        timeout.tv_usec = (SELECT_POLL_TIMEOUT_MS % 1000) * 1000;
#if defined(_WIN32)
        const auto its_result = ::select(0, &read_fds, &write_fds, &except_fds, &timeout);
#else
        const auto its_result = ::select(static_cast<int>(_socket) + 1, &read_fds, &write_fds, &except_fds, &timeout);
#endif
#endif

        if (its_result > 0) {
#if defined(VSOMEIP_ENABLE_XNET)
            bool is_ready = false;
            if (_wait == boost::asio::ip::tcp::socket::wait_read) {
                is_ready = (nxFD_ISSET(_socket, &read_fds) != 0);
            } else if (_wait == boost::asio::ip::tcp::socket::wait_write) {
                is_ready = (nxFD_ISSET(_socket, &write_fds) != 0);
            } else {
                is_ready = (nxFD_ISSET(_socket, &except_fds) != 0);
            }
            if (!is_ready) {
                continue;
            }
#endif
            _ec.clear();
            return true;
        }
        if (its_result == 0) {
            if (_stop_requested.load(std::memory_order_relaxed)
                || _cancel_epoch.load(std::memory_order_relaxed) != _operation_epoch) {
                _ec = boost::asio::error::operation_aborted;
                return false;
            }
            continue;
        }

        _ec = make_xnet_error("wait_socket_ready");
        if (_ec == boost::asio::error::interrupted) {
            continue;
        }
        return false;
    }
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

boost::system::error_code make_unsupported_option_error(char const* _option, char const* _reason) {
    VSOMEIP_WARNING << "[XNET][tcp][" << _option << "] unsupported: " << _reason;
    return boost::asio::error::make_error_code(boost::asio::error::operation_not_supported);
}

} // namespace

xnet_tcp_socket::xnet_tcp_socket(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack) :
    xnet_tcp_socket(_io, _xnet_stack, {}) {
}

xnet_tcp_socket::xnet_tcp_socket(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack, std::shared_ptr<void> _stack_lifetime)
    : socket_(INVALID_SOCKET_VALUE),
      io_context_(_io),
      xnet_stack_(_xnet_stack),
      stack_lifetime_(std::move(_stack_lifetime)),
      is_ipv6_(false),
      non_blocking_mode_(false),
      rx_stop_requested_(false),
      tx_stop_requested_(false),
      cancel_epoch_(0) {
    VSOMEIP_INFO << "[XNET][tcp][ctor] socket created";
}

xnet_tcp_socket::~xnet_tcp_socket() {
    boost::system::error_code ec;
    close(ec);
}

void xnet_tcp_socket::ensure_rx_worker_thread() {
    std::lock_guard<std::mutex> its_lock(rx_worker_mutex_);
    if (rx_worker_thread_.joinable()) {
        return;
    }

    rx_stop_requested_.store(false, std::memory_order_relaxed);
    rx_worker_thread_ = std::thread([this]() { rx_worker_loop(); });
}

void xnet_tcp_socket::ensure_tx_worker_thread() {
    std::lock_guard<std::mutex> its_lock(tx_worker_mutex_);
    if (tx_worker_thread_.joinable()) {
        return;
    }

    tx_stop_requested_.store(false, std::memory_order_relaxed);
    tx_worker_thread_ = std::thread([this]() { tx_worker_loop(); });
}

void xnet_tcp_socket::stop_worker_threads() {
    {
        std::lock_guard<std::mutex> its_lock(rx_worker_mutex_);
        rx_stop_requested_.store(true, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> its_lock(tx_worker_mutex_);
        tx_stop_requested_.store(true, std::memory_order_relaxed);
    }

    rx_worker_cv_.notify_all();
    tx_worker_cv_.notify_all();

    if (rx_worker_thread_.joinable()) {
        rx_worker_thread_.join();
    }
    if (tx_worker_thread_.joinable()) {
        tx_worker_thread_.join();
    }

    {
        std::lock_guard<std::mutex> its_lock(rx_worker_mutex_);
        rx_work_queue_.clear();
    }
    {
        std::lock_guard<std::mutex> its_lock(tx_worker_mutex_);
        tx_work_queue_.clear();
    }
}

bool xnet_tcp_socket::enqueue_rx_work(work_item_t&& _item) {
    ensure_rx_worker_thread();
    {
        std::lock_guard<std::mutex> its_lock(rx_worker_mutex_);
        if (rx_stop_requested_.load(std::memory_order_relaxed)) {
            return false;
        }
        rx_work_queue_.emplace_back(std::move(_item));
    }
    rx_worker_cv_.notify_one();
    return true;
}

bool xnet_tcp_socket::enqueue_tx_work(work_item_t&& _item) {
    ensure_tx_worker_thread();
    {
        std::lock_guard<std::mutex> its_lock(tx_worker_mutex_);
        if (tx_stop_requested_.load(std::memory_order_relaxed)) {
            return false;
        }
        tx_work_queue_.emplace_back(std::move(_item));
    }
    tx_worker_cv_.notify_one();
    return true;
}

void xnet_tcp_socket::rx_worker_loop() {
    for (;;) {
        work_item_t its_item;
        {
            std::unique_lock<std::mutex> its_lock(rx_worker_mutex_);
            rx_worker_cv_.wait(its_lock, [this]() {
                return rx_stop_requested_.load(std::memory_order_relaxed) || !rx_work_queue_.empty();
            });

            if (rx_stop_requested_.load(std::memory_order_relaxed) && rx_work_queue_.empty()) {
                return;
            }

            its_item = std::move(rx_work_queue_.front());
            rx_work_queue_.pop_front();
        }

        if (its_item) {
            its_item();
        }
    }
}

void xnet_tcp_socket::tx_worker_loop() {
    for (;;) {
        work_item_t its_item;
        {
            std::unique_lock<std::mutex> its_lock(tx_worker_mutex_);
            tx_worker_cv_.wait(its_lock, [this]() {
                return tx_stop_requested_.load(std::memory_order_relaxed) || !tx_work_queue_.empty();
            });

            if (tx_stop_requested_.load(std::memory_order_relaxed) && tx_work_queue_.empty()) {
                return;
            }

            its_item = std::move(tx_work_queue_.front());
            tx_work_queue_.pop_front();
        }

        if (its_item) {
            its_item();
        }
    }
}

bool xnet_tcp_socket::is_open() const {
    return socket_ != INVALID_SOCKET_VALUE;
}

int xnet_tcp_socket::native_handle() {
    return static_cast<int>(socket_);
}

void xnet_tcp_socket::assign_accepted_socket(nxSOCKET _socket, bool _is_ipv6, boost::system::error_code& _ec) {
    if (is_open()) {
        close(_ec);
        if (_ec) {
            return;
        }
    }

    auto its_socket = _socket;

#if defined(VSOMEIP_ENABLE_XNET)
    int opt = 0;
    if (xnet_api::nxsetsockopt(its_socket, nxSOL_SOCKET, nxSO_NONBLOCK, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("assign_accepted_socket:non_blocking");
        return;
    }
#else
#if defined(_WIN32)
    u_long non_blocking = 1UL;
    if (::ioctlsocket(static_cast<SOCKET>(its_socket), FIONBIO, &non_blocking) != 0) {
        _ec = make_xnet_error("assign_accepted_socket:non_blocking");
        return;
    }
#else
    int flags = ::fcntl(static_cast<int>(its_socket), F_GETFL, 0);
    if (flags == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("assign_accepted_socket:non_blocking");
        return;
    }
    if (::fcntl(static_cast<int>(its_socket), F_SETFL, flags | O_NONBLOCK) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("assign_accepted_socket:non_blocking");
        return;
    }
#endif
#endif

    socket_ = its_socket;
    is_ipv6_ = _is_ipv6;
    non_blocking_mode_ = false;
    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);
    _ec.clear();
}

void xnet_tcp_socket::open(boost::asio::ip::tcp::endpoint::protocol_type _pt, boost::system::error_code& _ec) {
    if (is_open()) {
        close(_ec);
        if (_ec) {
            return;
        }
    }

    is_ipv6_ = (_pt == boost::asio::ip::tcp::v6());

#if defined(VSOMEIP_ENABLE_XNET)
    socket_ = xnet_api::nxsocket(xnet_stack_, is_ipv6_ ? nxAF_INET6 : nxAF_INET, nxSOCK_STREAM, nxIPPROTO_TCP);
    const bool is_invalid_socket = (socket_ == nxINVALID_SOCKET);
#else
    socket_ = ::socket(is_ipv6_ ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
#if defined(_WIN32)
    const bool is_invalid_socket = (socket_ == static_cast<nxSOCKET>(INVALID_SOCKET));
#else
    const bool is_invalid_socket = (socket_ == INVALID_SOCKET_VALUE);
#endif
#endif

    if (is_invalid_socket) {
        socket_ = INVALID_SOCKET_VALUE;
        _ec = make_xnet_error("open");
        return;
    }

#if defined(VSOMEIP_ENABLE_XNET)
    int opt = 0;
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_NONBLOCK, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        const auto its_non_blocking_error = make_xnet_error("open:non_blocking");
        boost::system::error_code its_close_error;
        close(its_close_error);
        _ec = its_non_blocking_error;
        return;
    }
#else
#if defined(_WIN32)
    u_long non_blocking = 1UL;
    if (::ioctlsocket(static_cast<SOCKET>(socket_), FIONBIO, &non_blocking) != 0) {
        const auto its_non_blocking_error = make_xnet_error("open:non_blocking");
        boost::system::error_code its_close_error;
        close(its_close_error);
        _ec = its_non_blocking_error;
        return;
    }
#else
    int flags = ::fcntl(static_cast<int>(socket_), F_GETFL, 0);
    if (flags == SOCKET_ERROR_VALUE) {
        const auto its_non_blocking_error = make_xnet_error("open:non_blocking");
        boost::system::error_code its_close_error;
        close(its_close_error);
        _ec = its_non_blocking_error;
        return;
    }
    if (::fcntl(static_cast<int>(socket_), F_SETFL, flags | O_NONBLOCK) == SOCKET_ERROR_VALUE) {
        const auto its_non_blocking_error = make_xnet_error("open:non_blocking");
        boost::system::error_code its_close_error;
        close(its_close_error);
        _ec = its_non_blocking_error;
        return;
    }
#endif
#endif

    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);
    non_blocking_mode_ = false;
    _ec.clear();
}

void xnet_tcp_socket::bind(boost::asio::ip::tcp::endpoint const& _ep, boost::system::error_code& _ec) {
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
    if (xnet_api::nxbind(socket_, reinterpret_cast<nxsockaddr*>(&its_storage), its_len) == SOCKET_ERROR_VALUE) {
#else
    if (::bind(static_cast<int>(socket_), reinterpret_cast<sockaddr*>(&its_storage), static_cast<socklen_t>(its_len))
        == SOCKET_ERROR_VALUE) {
#endif
        _ec = make_xnet_error("bind");
        return;
    }

    _ec.clear();
}

void xnet_tcp_socket::close(boost::system::error_code& _ec) {
    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);

    // Guarantee that the worker threads are always stopped and joined, even if
    // the backend close call below fails and the function returns early. A
    // joinable std::thread destroyed during socket teardown would otherwise
    // call std::terminate().
    struct worker_stop_guard {
        xnet_tcp_socket* self;
        ~worker_stop_guard() { self->stop_worker_threads(); }
    } its_worker_stop_guard{this};

    if (!is_open()) {
        _ec.clear();
        return;
    }

#if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxclose(socket_) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("close");
        return;
    }
#else
#if defined(_WIN32)
    if (::closesocket(static_cast<SOCKET>(socket_)) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("close");
        return;
    }
#else
    if (::close(static_cast<int>(socket_)) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("close");
        return;
    }
#endif
#endif

    socket_ = INVALID_SOCKET_VALUE;
    non_blocking_mode_ = false;
    _ec.clear();
}

void xnet_tcp_socket::cancel(boost::system::error_code& _ec) {
    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);
    _ec.clear();
}

boost::asio::ip::tcp::endpoint xnet_tcp_socket::local_endpoint(boost::system::error_code& _ec) const {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return {};
    }

    nxsockaddr_storage its_storage{};
    nxsocklen_t its_len = static_cast<nxsocklen_t>(sizeof(its_storage));

#if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxgetsockname(socket_, reinterpret_cast<nxsockaddr*>(&its_storage), &its_len) == SOCKET_ERROR_VALUE) {
#else
    if (::getsockname(static_cast<int>(socket_), reinterpret_cast<sockaddr*>(&its_storage), reinterpret_cast<socklen_t*>(&its_len))
        == SOCKET_ERROR_VALUE) {
#endif
        _ec = make_xnet_error("local_endpoint");
        return {};
    }

    boost::asio::ip::tcp::endpoint its_endpoint;
    if (!native_to_endpoint(its_storage, its_len, its_endpoint, _ec)) {
        return {};
    }

    _ec.clear();
    return its_endpoint;
}

void xnet_tcp_socket::io_control(io_control_operation<std::size_t>& _icm, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

#if defined(VSOMEIP_ENABLE_XNET)
    int32_t rx_data = 0;
    nxsocklen_t opt_len = static_cast<nxsocklen_t>(sizeof(rx_data));
    if (xnet_api::nxgetsockopt(socket_, nxSOL_SOCKET, nxSO_RXDATA, &rx_data, &opt_len) == SOCKET_ERROR_VALUE
        || opt_len < static_cast<nxsocklen_t>(sizeof(rx_data))) {
        _ec = make_xnet_error("io_control");
        return;
    }
    _icm.set(static_cast<std::size_t>(rx_data < 0 ? 0 : rx_data));
    _ec.clear();
    return;
#endif

    _ec = make_unsupported_option_error("io_control", "unsupported ioctl command");
}

void xnet_tcp_socket::set_option(boost::asio::ip::tcp::no_delay _nd, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    int opt = _nd.value() ? 1 : 0;
#if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_TCP, nxTCP_NODELAY, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
#else
    if (::setsockopt(static_cast<int>(socket_), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt),
                     static_cast<socklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
#endif
        _ec = make_xnet_error("set_option:no_delay");
        return;
    }

    _ec.clear();
}

void xnet_tcp_socket::set_option(boost::asio::ip::tcp::socket::keep_alive _ka, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

#if defined(VSOMEIP_ENABLE_XNET)
    _ec = make_unsupported_option_error("set_option:keep_alive",
                                        _ka.value() ? "enable keepalive is not supported by XNET socket API"
                                                    : "disable keepalive is not supported by XNET socket API");
#else
    int opt = _ka.value() ? 1 : 0;
    if (::setsockopt(static_cast<int>(socket_), SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&opt),
                     static_cast<socklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("set_option:keep_alive");
        return;
    }
    _ec.clear();
#endif
}

void xnet_tcp_socket::set_option(boost::asio::ip::tcp::socket::linger _l, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

#if defined(VSOMEIP_ENABLE_XNET)
    nxlinger linger_opt{};
    linger_opt.l_onoff = _l.enabled() ? 1 : 0;
    linger_opt.l_linger = _l.timeout();
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_LINGER, &linger_opt, static_cast<nxsocklen_t>(sizeof(linger_opt)))
        == SOCKET_ERROR_VALUE) {
#else
    linger linger_opt{};
    linger_opt.l_onoff = _l.enabled() ? 1 : 0;
    linger_opt.l_linger = _l.timeout();
    if (::setsockopt(static_cast<int>(socket_), SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&linger_opt),
                     static_cast<socklen_t>(sizeof(linger_opt))) == SOCKET_ERROR_VALUE) {
#endif
        _ec = make_xnet_error("set_option:linger");
        return;
    }

    _ec.clear();
}

void xnet_tcp_socket::set_option(boost::asio::ip::tcp::socket::reuse_address _ra, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    int opt = _ra.value() ? 1 : 0;
#if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_REUSEADDR, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
#else
    if (::setsockopt(static_cast<int>(socket_), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt),
                     static_cast<socklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
#endif
        _ec = make_xnet_error("set_option:reuse_address");
        return;
    }

    _ec.clear();
}

void xnet_tcp_socket::async_connect(boost::asio::ip::tcp::endpoint const& _ep, connect_handler _handler) {
    if (!is_open()) {
        post_completion(io_context_, std::move(_handler), boost::asio::error::bad_descriptor);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto* its_io = &io_context_;
    if (!enqueue_tx_work([this, its_io, its_socket, its_epoch, endpoint = _ep, handler = std::move(_handler)]() mutable {
        boost::system::error_code its_error;

        nxsockaddr_storage its_storage{};
        nxsocklen_t its_length = 0;
        if (!endpoint_to_native(endpoint, its_storage, its_length, its_error)) {
            post_completion(*its_io, std::move(handler), its_error);
            return;
        }

#if defined(VSOMEIP_ENABLE_XNET)
        const auto its_result = xnet_api::nxconnect(its_socket, reinterpret_cast<nxsockaddr*>(&its_storage), its_length);
#else
        const auto its_result = ::connect(static_cast<int>(its_socket), reinterpret_cast<sockaddr*>(&its_storage),
                                          static_cast<socklen_t>(its_length));
#endif

        if (its_result == SOCKET_ERROR_VALUE) {
            its_error = make_xnet_error("async_connect");
            if (is_would_block_like(its_error)) {
                boost::system::error_code its_wait_error;
                if (wait_socket_ready(its_socket, boost::asio::ip::tcp::socket::wait_write,
                                      tx_stop_requested_, cancel_epoch_, its_epoch, its_wait_error)) {
                    int32_t its_so_error = 0;
#if defined(VSOMEIP_ENABLE_XNET)
                    nxsocklen_t its_so_error_len = static_cast<nxsocklen_t>(sizeof(its_so_error));
                    if (xnet_api::nxgetsockopt(its_socket, nxSOL_SOCKET, nxSO_ERROR, &its_so_error, &its_so_error_len) == SOCKET_ERROR_VALUE
                        || its_so_error_len < static_cast<nxsocklen_t>(sizeof(its_so_error))) {
                        its_error = make_xnet_error("async_connect:so_error_query");
                    } else if (its_so_error != 0) {
                        its_error = xnet_to_boost_error(static_cast<int>(its_so_error));
                    } else {
                        its_error.clear();
                    }
#else
                    socklen_t its_so_error_len = static_cast<socklen_t>(sizeof(its_so_error));
                    if (::getsockopt(static_cast<int>(its_socket), SOL_SOCKET, SO_ERROR,
                                     reinterpret_cast<char*>(&its_so_error), &its_so_error_len) == SOCKET_ERROR_VALUE
                        || its_so_error_len < static_cast<socklen_t>(sizeof(its_so_error))) {
                        its_error = make_xnet_error("async_connect:so_error_query");
                    } else if (its_so_error != 0) {
                        its_error = xnet_to_boost_error(static_cast<int>(its_so_error));
                    } else {
                        its_error.clear();
                    }
#endif
                } else {
                    its_error = its_wait_error;
                }
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
        }

        post_completion(*its_io, std::move(handler), its_error);
    })) {
        post_completion(io_context_, std::move(_handler), boost::asio::error::operation_aborted);
    }
}

void xnet_tcp_socket::async_receive(boost::asio::mutable_buffer _b, rw_handler _handler) {
    if (!is_open()) {
        post_rw_completion(io_context_, std::move(_handler), boost::asio::error::bad_descriptor, 0);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto receive_data = std::make_shared<std::vector<std::uint8_t>>(_b.size());
    const auto its_buffer_size = static_cast<int32_t>(clamp_size_to_int32(receive_data->size()));

    auto* its_io = &io_context_;
    if (!enqueue_rx_work([this, its_io, its_socket, its_epoch, caller_buffer = _b, receive_data, its_buffer_size,
                       handler = std::move(_handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t its_bytes_received = 0;

        for (;;) {
            auto* const its_buffer = receive_data->empty() ? nullptr : receive_data->data();
#if defined(VSOMEIP_ENABLE_XNET)
            const auto its_result = xnet_api::nxrecv(its_socket, its_buffer, its_buffer_size, 0);
#else
            const auto its_result = ::recv(static_cast<int>(its_socket), reinterpret_cast<char*>(its_buffer), its_buffer_size, 0);
#endif

            if (its_result > 0) {
                its_bytes_received = static_cast<std::size_t>(its_result);
                its_error.clear();
                break;
            }

            if (its_result == 0) {
                its_error = boost::asio::error::eof;
                break;
            }

            its_error = make_xnet_error("async_receive");
            if (its_error == boost::asio::error::not_connected) {
                its_error = boost::asio::error::eof;
            }
            if (!is_would_block_like(its_error)) {
                break;
            }

            boost::system::error_code its_wait_error;
            if (!wait_socket_ready(its_socket, boost::asio::ip::tcp::socket::wait_read,
                                   rx_stop_requested_, cancel_epoch_, its_epoch, its_wait_error)) {
                its_error = its_wait_error;
                break;
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
            its_bytes_received = 0;
        }

        std::size_t posted_bytes = its_bytes_received;
        if (!its_error) {
            posted_bytes = std::min(posted_bytes, caller_buffer.size());
        }

        auto its_executor = boost::asio::get_associated_executor(handler, its_io->get_executor());
        boost::asio::post(its_executor,
                          [handler = std::move(handler), ec = its_error, bytes = posted_bytes,
                           caller_buffer, receive_data]() mutable {
            if (!ec && bytes > 0 && caller_buffer.data() != nullptr && !receive_data->empty()) {
                std::memcpy(caller_buffer.data(), receive_data->data(), bytes);
            }
            handler(ec, ec ? 0 : bytes);
        });
    })) {
        post_rw_completion(io_context_, std::move(_handler), boost::asio::error::operation_aborted, 0);
    }
}

void xnet_tcp_socket::async_write(std::vector<boost::asio::const_buffer> const& _bs, rw_handler _handler) {
    if (!is_open()) {
        post_rw_completion(io_context_, std::move(_handler), boost::asio::error::bad_descriptor, 0);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto buffers = std::make_shared<std::vector<std::vector<std::uint8_t>>>();
    buffers->reserve(_bs.size());
    for (auto const& b : _bs) {
        std::vector<std::uint8_t> its_owned_buffer;
        if (b.size() > 0) {
            auto* its_data = static_cast<const std::uint8_t*>(b.data());
            if (its_data == nullptr) {
                post_rw_completion(io_context_, std::move(_handler), boost::asio::error::invalid_argument, 0);
                return;
            }
            its_owned_buffer.assign(its_data, its_data + b.size());
        }
        buffers->emplace_back(std::move(its_owned_buffer));
    }

    auto* its_io = &io_context_;
    if (!enqueue_tx_work([this, its_io, its_socket, its_epoch, buffers, handler = std::move(_handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t total_sent = 0;

        for (const auto& b : *buffers) {
            auto* const data_ptr = b.empty() ? nullptr : b.data();
            std::size_t sent_in_buffer = 0;

            while (sent_in_buffer < b.size()) {
                if (is_canceled(its_epoch)) {
                    its_error = boost::asio::error::operation_aborted;
                    total_sent = 0;
                    post_rw_completion(*its_io, std::move(handler), its_error, total_sent);
                    return;
                }

                const auto remaining = b.size() - sent_in_buffer;
                const auto chunk = static_cast<int32_t>(clamp_size_to_int32(remaining));

#if defined(VSOMEIP_ENABLE_XNET)
                const auto result = xnet_api::nxsend(its_socket, data_ptr + sent_in_buffer, chunk, 0);
#else
                const auto result = ::send(static_cast<int>(its_socket), reinterpret_cast<const char*>(data_ptr + sent_in_buffer),
                                           chunk, 0);
#endif
                if (result > 0) {
                    const auto sent = static_cast<std::size_t>(result);
                    sent_in_buffer += sent;
                    total_sent += sent;
                    continue;
                }

                if (result == 0) {
                    its_error = boost::asio::error::eof;
                    post_rw_completion(*its_io, std::move(handler), its_error, total_sent);
                    return;
                }

                its_error = make_xnet_error("async_write(sequence)");
                if (!is_would_block_like(its_error)) {
                    post_rw_completion(*its_io, std::move(handler), its_error, is_canceled(its_epoch) ? 0 : total_sent);
                    return;
                }

                boost::system::error_code its_wait_error;
                if (!wait_socket_ready(its_socket, boost::asio::ip::tcp::socket::wait_write,
                                       tx_stop_requested_, cancel_epoch_, its_epoch, its_wait_error)) {
                    its_error = its_wait_error;
                    post_rw_completion(*its_io, std::move(handler), its_error, its_error == boost::asio::error::operation_aborted ? 0 : total_sent);
                    return;
                }
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
            total_sent = 0;
        }

        post_rw_completion(*its_io, std::move(handler), its_error, total_sent);
    })) {
        post_rw_completion(io_context_, std::move(_handler), boost::asio::error::operation_aborted, 0);
    }
}

void xnet_tcp_socket::async_write(boost::asio::const_buffer const& _b, completion_condition _cc, rw_handler _handler) {
    if (!is_open()) {
        post_rw_completion(io_context_, std::move(_handler), boost::asio::error::bad_descriptor, 0);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);

    auto data = std::make_shared<std::vector<std::uint8_t>>();
    if (_b.size() > 0) {
        auto* ptr = static_cast<const std::uint8_t*>(_b.data());
        if (ptr == nullptr) {
            post_rw_completion(io_context_, std::move(_handler), boost::asio::error::invalid_argument, 0);
            return;
        }
        data->assign(ptr, ptr + _b.size());
    }

    auto* its_io = &io_context_;
    if (!enqueue_tx_work([this, its_io, its_socket, its_epoch, data, cc = std::move(_cc), handler = std::move(_handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t total_sent = 0;

        while (total_sent < data->size()) {
            if (is_canceled(its_epoch)) {
                its_error = boost::asio::error::operation_aborted;
                total_sent = 0;
                break;
            }

            const auto its_remaining = data->size() - total_sent;
            const auto its_chunk = static_cast<int32_t>(clamp_size_to_int32(its_remaining));

#if defined(VSOMEIP_ENABLE_XNET)
            const auto its_result = xnet_api::nxsend(its_socket, data->data() + total_sent, its_chunk, 0);
#else
            const auto its_result = ::send(static_cast<int>(its_socket), reinterpret_cast<const char*>(data->data() + total_sent),
                                           its_chunk, 0);
#endif

            if (its_result > 0) {
                total_sent += static_cast<std::size_t>(its_result);
                auto next = cc(its_error, total_sent);
                if (next == 0) {
                    break;
                }
                continue;
            }

            if (its_result == 0) {
                its_error = boost::asio::error::eof;
                break;
            }

            its_error = make_xnet_error("async_write");
            if (!is_would_block_like(its_error)) {
                break;
            }

            boost::system::error_code its_wait_error;
            if (!wait_socket_ready(its_socket, boost::asio::ip::tcp::socket::wait_write,
                                   tx_stop_requested_, cancel_epoch_, its_epoch, its_wait_error)) {
                its_error = its_wait_error;
                break;
            }
        }

        if (its_error == boost::asio::error::operation_aborted) {
            total_sent = 0;
        }

        post_rw_completion(*its_io, std::move(handler), its_error, total_sent);
    })) {
        post_rw_completion(io_context_, std::move(_handler), boost::asio::error::operation_aborted, 0);
    }
}

#if defined(__linux__)
bool xnet_tcp_socket::set_user_timeout(unsigned int _timeout) {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    VSOMEIP_WARNING << "[XNET][tcp][set_user_timeout] unsupported by XNET API, timeout=" << _timeout;
    errno = ENOTSUP;
    return false;
}

bool xnet_tcp_socket::set_keepidle(uint32_t _idle) {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    VSOMEIP_WARNING << "[XNET][tcp][set_keepidle] unsupported by XNET API, idle=" << _idle;
    errno = ENOTSUP;
    return false;
}

bool xnet_tcp_socket::set_keepintvl(uint32_t _interval) {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    VSOMEIP_WARNING << "[XNET][tcp][set_keepintvl] unsupported by XNET API, interval=" << _interval;
    errno = ENOTSUP;
    return false;
}

bool xnet_tcp_socket::set_keepcnt(uint32_t _count) {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    VSOMEIP_WARNING << "[XNET][tcp][set_keepcnt] unsupported by XNET API, count=" << _count;
    errno = ENOTSUP;
    return false;
}

bool xnet_tcp_socket::set_quick_ack() {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    VSOMEIP_WARNING << "[XNET][tcp][set_quick_ack] unsupported by XNET API";
    errno = ENOTSUP;
    return false;
}
#endif

#if defined(__linux__) || defined(__QNX__)
bool xnet_tcp_socket::bind_to_device(std::string const& _device) {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

#if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_BINDTODEVICE, _device.c_str(), static_cast<nxsocklen_t>(_device.size()))
        == SOCKET_ERROR_VALUE) {
        (void)make_xnet_error("bind_to_device");
        return false;
    }
#else
    if (!socket_option_helpers::set_bind_to_device(static_cast<int>(socket_), _device)) {
        (void)make_xnet_error("bind_to_device");
        return false;
    }
#endif

    return true;
}

bool xnet_tcp_socket::can_read_fd_flags() {
    return true;
}
#endif

bool xnet_tcp_socket::is_canceled(std::uint64_t _epoch) const {
    return cancel_epoch_.load(std::memory_order_relaxed) != _epoch;
}

}

