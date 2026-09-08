#include <iostream>
#include <cstring>
#include <algorithm>
#include <condition_variable>
#include <limits>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/endian/conversion.hpp>

#if !defined(VSOMEIP_ENABLE_XNET)
#include <sys/types.h>
#include <sys/socket.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#endif

#include <boost/asio/defer.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/buffer.hpp>

#include "../include/xnet_udp_socket.hpp"
#include "../include/xnet_error.hpp"
#include "../include/xnet_api.hpp"

#include "logger_ext.hpp"

#define VSOMEIP_LOG_PREFIX "xnu"

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

constexpr char const* k_xnet_backend_tag = "backend=xnet";

boost::system::error_code make_xnet_error(char const* _operation);

constexpr int SELECT_POLL_TIMEOUT_MS = 200;

inline std::size_t clamp_size_to_int32(std::size_t _size) {
    return std::min(_size, static_cast<std::size_t>(std::numeric_limits<int32_t>::max()));
}

inline bool is_would_block_like(boost::system::error_code const& _ec) {
    return _ec == boost::asio::error::would_block || _ec == boost::asio::error::try_again || _ec == boost::asio::error::in_progress;
}

void post_completion(boost::asio::io_context& _io, udp_socket::completion_handler _handler, boost::system::error_code const& _ec) {
    auto its_executor = boost::asio::get_associated_executor(_handler, _io.get_executor());
    boost::asio::post(its_executor, [handler = std::move(_handler), ec = _ec]() mutable { handler(ec); });
}

void post_rw_completion(boost::asio::io_context& _io, udp_socket::rw_handler _handler, boost::system::error_code const& _ec,
                        std::size_t _bytes) {
    auto its_executor = boost::asio::get_associated_executor(_handler, _io.get_executor());
    boost::asio::post(its_executor, [handler = std::move(_handler), ec = _ec, bytes = _bytes]() mutable { handler(ec, bytes); });
}

void post_receive_from_completion(boost::asio::io_context& _io,
                                  udp_socket::rw_handler _handler,
                                  boost::system::error_code const& _ec,
                                  std::size_t _bytes,
                                  boost::asio::mutable_buffer _caller_buffer,
                                  boost::asio::ip::udp::endpoint* _caller_remote,
                                  std::shared_ptr<std::vector<std::uint8_t>> _received_data,
                                  std::shared_ptr<boost::asio::ip::udp::endpoint> _source_endpoint) {
    auto its_executor = boost::asio::get_associated_executor(_handler, _io.get_executor());
    boost::asio::post(its_executor,
                      [handler = std::move(_handler), ec = _ec, bytes = _bytes, caller_buffer = _caller_buffer,
                       caller_remote = _caller_remote, received_data = std::move(_received_data),
                       source_endpoint = std::move(_source_endpoint)]() mutable {
        std::size_t its_bytes = bytes;
        if (!ec) {
            its_bytes = std::min(its_bytes, caller_buffer.size());
            if (its_bytes > 0 && caller_buffer.data() != nullptr && !received_data->empty()) {
                std::memcpy(caller_buffer.data(), received_data->data(), its_bytes);
            }
            if (caller_remote != nullptr) {
                *caller_remote = *source_endpoint;
            }
        } else {
            its_bytes = 0;
        }

        handler(ec, its_bytes);
    });
}

bool wait_socket_ready(nxSOCKET _socket, boost::asio::ip::udp::socket::wait_type _wait,
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

        if (_wait == boost::asio::ip::udp::socket::wait_read) {
            nxFD_SET(_socket, &read_fds);
        } else if (_wait == boost::asio::ip::udp::socket::wait_write) {
            nxFD_SET(_socket, &write_fds);
        } else {
            nxFD_SET(_socket, &except_fds);
        }

        nxtimeval timeout{};
        timeout.tv_sec = SELECT_POLL_TIMEOUT_MS / 1000;
        timeout.tv_usec = (SELECT_POLL_TIMEOUT_MS % 1000) * 1000;
        const auto its_result = xnet_api::nxselect(0, &read_fds, &write_fds, &except_fds, &timeout);
#else
        fd_set read_fds{};
        fd_set write_fds{};
        fd_set except_fds{};
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_ZERO(&except_fds);

        if (_wait == boost::asio::ip::udp::socket::wait_read) {
            FD_SET(static_cast<int>(_socket), &read_fds);
        } else if (_wait == boost::asio::ip::udp::socket::wait_write) {
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

bool endpoint_to_native(boost::asio::ip::udp::endpoint const& _endpoint, nxsockaddr_storage& _storage, nxsocklen_t& _len,
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

bool native_to_endpoint(nxsockaddr_storage const& _storage, nxsocklen_t _len, boost::asio::ip::udp::endpoint& _endpoint,
                        boost::system::error_code& _ec) {
    const auto* its_sockaddr = reinterpret_cast<const nxsockaddr*>(&_storage);
    if (its_sockaddr->sa_family == nxAF_INET && static_cast<std::size_t>(_len) >= sizeof(nxsockaddr_in)) {
        const auto* its_addr = reinterpret_cast<const nxsockaddr_in*>(&_storage);
        const auto its_ip = boost::asio::ip::address_v4(boost::endian::big_to_native(its_addr->sin_addr.addr));
        const auto its_port = boost::endian::big_to_native(its_addr->sin_port);
        _endpoint = boost::asio::ip::udp::endpoint(its_ip, its_port);
        _ec.clear();
        return true;
    }

    if (its_sockaddr->sa_family == nxAF_INET6 && static_cast<std::size_t>(_len) >= sizeof(nxsockaddr_in6)) {
        const auto* its_addr = reinterpret_cast<const nxsockaddr_in6*>(&_storage);
        boost::asio::ip::address_v6::bytes_type its_bytes{};
        std::memcpy(its_bytes.data(), its_addr->sin6_addr.addr, its_bytes.size());
        const auto its_ip = boost::asio::ip::address_v6(its_bytes, its_addr->sin6_scope_id);
        const auto its_port = boost::endian::big_to_native(its_addr->sin6_port);
        _endpoint = boost::asio::ip::udp::endpoint(its_ip, its_port);
        _ec.clear();
        return true;
    }

    _ec = boost::asio::error::make_error_code(boost::asio::error::address_family_not_supported);
    return false;
}

boost::system::error_code make_xnet_error(char const* _operation) {
    const auto its_raw_error = xnet_get_last_error();
    auto its_mapped_error = xnet_to_boost_error(its_raw_error);
    if (its_mapped_error != boost::asio::error::would_block &&
        its_mapped_error != boost::asio::error::try_again &&
        its_mapped_error != boost::asio::error::in_progress) {
        VSOMEIP_ERROR << "[XNET][udp][" << _operation << "] " << k_xnet_backend_tag
                      << " failure_class=stack_io"
                      << " raw_error=" << its_raw_error
                      << " mapped_error=" << its_mapped_error.value()
                      << " message=" << its_mapped_error.message();
    }
    return its_mapped_error;
}

boost::system::error_code make_unsupported_option_error(char const* _option, char const* _reason) {
    VSOMEIP_WARNING << "[XNET][udp][" << _option << "] " << k_xnet_backend_tag
                    << " failure_class=option_translation"
                    << " detail=unsupported"
                    << " reason=" << _reason;
    std::cerr << "[vsomeip] [XNET][udp][" << _option << "] " << k_xnet_backend_tag
              << " failure_class=option_translation"
              << " detail=unsupported"
              << " reason=" << _reason << std::endl;
    return boost::asio::error::make_error_code(boost::asio::error::operation_not_supported);
}

boost::system::error_code make_family_mismatch_error(char const* _option, bool _is_ipv6_socket) {
    VSOMEIP_ERROR << "[XNET][udp][" << _option << "] " << k_xnet_backend_tag
                  << " failure_class=option_translation"
                  << " detail=address_family_mismatch"
                  << " socket_family=" << (_is_ipv6_socket ? "IPv6" : "IPv4");
    return boost::asio::error::make_error_code(boost::asio::error::address_family_not_supported);
}

} // namespace

xnet_udp_socket::xnet_udp_socket(boost::asio::io_context& _io, nxIpStackRef_t xnet_stack) :
    xnet_udp_socket(_io, xnet_stack, {}) {
}

xnet_udp_socket::xnet_udp_socket(boost::asio::io_context& _io, nxIpStackRef_t xnet_stack, std::shared_ptr<void> _stack_lifetime)
    : socket_(INVALID_SOCKET_VALUE),
      io_context_(_io),
      xnet_stack_(xnet_stack),
      stack_lifetime_(std::move(_stack_lifetime)),
      is_ipv6_(false),
      non_blocking_mode_(false),
      stop_requested_(false),
      cancel_epoch_(0) {
    VSOMEIP_INFO << "[XNET][udp][ctor] " << k_xnet_backend_tag
                 << " stack_ref=" << xnet_stack_
                 << " stack_ready=" << (xnet_stack_ != nullptr ? "true" : "false");
}

xnet_udp_socket::~xnet_udp_socket() {
    boost::system::error_code ec;
    close(ec);
}

void xnet_udp_socket::ensure_general_worker_thread() {
    std::lock_guard<std::mutex> its_lock(general_worker_mutex_);
    if (general_worker_thread_.joinable()) {
        return;
    }

    stop_requested_.store(false, std::memory_order_relaxed);
    general_worker_thread_ = std::thread([this]() { general_worker_loop(); });
}

void xnet_udp_socket::ensure_receive_worker_thread() {
    {
        std::lock_guard<std::mutex> its_lock(receive_worker_mutex_);
        if (receive_worker_thread_.joinable()) {
            return;
        }

        stop_requested_.store(false, std::memory_order_relaxed);
        receive_worker_thread_ = std::thread([this]() { receive_worker_loop(); });
    }
}

void xnet_udp_socket::stop_worker_threads() {
    {
        std::lock_guard<std::mutex> its_lock(general_worker_mutex_);
        stop_requested_.store(true, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> its_lock(receive_worker_mutex_);
        stop_requested_.store(true, std::memory_order_relaxed);
    }

    general_worker_cv_.notify_all();
    receive_worker_cv_.notify_all();

    if (general_worker_thread_.joinable()) {
        general_worker_thread_.join();
    }
    if (receive_worker_thread_.joinable()) {
        receive_worker_thread_.join();
    }

    {
        std::lock_guard<std::mutex> its_lock(general_worker_mutex_);
        general_work_queue_.clear();
    }
    {
        std::lock_guard<std::mutex> its_lock(receive_worker_mutex_);
        receive_work_queue_.clear();
    }
}

bool xnet_udp_socket::enqueue_work(work_item_t&& _item) {
    ensure_general_worker_thread();
    {
        std::lock_guard<std::mutex> its_lock(general_worker_mutex_);
        if (stop_requested_.load(std::memory_order_relaxed)) {
            return false;
        }
        general_work_queue_.emplace_back(std::move(_item));
    }
    general_worker_cv_.notify_one();
    return true;
}

bool xnet_udp_socket::enqueue_receive_work(work_item_t&& _item) {
    ensure_receive_worker_thread();
    {
        std::lock_guard<std::mutex> its_lock(receive_worker_mutex_);
        if (stop_requested_.load(std::memory_order_relaxed)) {
            return false;
        }
        receive_work_queue_.emplace_back(std::move(_item));
    }
    receive_worker_cv_.notify_one();
    return true;
}

void xnet_udp_socket::general_worker_loop() {
    for (;;) {
        work_item_t its_item;
        {
            std::unique_lock<std::mutex> its_lock(general_worker_mutex_);
            general_worker_cv_.wait(its_lock, [this]() {
                return stop_requested_.load(std::memory_order_relaxed) || !general_work_queue_.empty();
            });

            if (stop_requested_.load(std::memory_order_relaxed) && general_work_queue_.empty()) {
                return;
            }

            its_item = std::move(general_work_queue_.front());
            general_work_queue_.pop_front();
        }

        if (its_item) {
            its_item();
        }
    }
}

void xnet_udp_socket::receive_worker_loop() {
    for (;;) {
        work_item_t its_item;
        {
            std::unique_lock<std::mutex> its_lock(receive_worker_mutex_);
            receive_worker_cv_.wait(its_lock, [this]() {
                return stop_requested_.load(std::memory_order_relaxed) || !receive_work_queue_.empty();
            });

            if (stop_requested_.load(std::memory_order_relaxed) && receive_work_queue_.empty()) {
                return;
            }

            its_item = std::move(receive_work_queue_.front());
            receive_work_queue_.pop_front();
        }

        if (its_item) {
            its_item();
        }
    }
}

bool xnet_udp_socket::is_open() const {
    return socket_ != INVALID_SOCKET_VALUE;
}

int xnet_udp_socket::native_handle() {
    return static_cast<int>(socket_);
}

void xnet_udp_socket::open(boost::asio::ip::udp::endpoint::protocol_type pt, boost::system::error_code& ec) {
    if (is_open()) { 
        close(ec); 
        if (ec) {
            return;
        }
    }

    is_ipv6_ = (pt == boost::asio::ip::udp::v6());

    #if defined(VSOMEIP_ENABLE_XNET)
    socket_ = xnet_api::nxsocket(xnet_stack_, is_ipv6_ ? nxAF_INET6 : nxAF_INET, nxSOCK_DGRAM, nxIPPROTO_UDP);
    #else
    socket_ = ::socket(is_ipv6_ ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    #endif

    #if defined(VSOMEIP_ENABLE_XNET)
    const bool is_invalid_socket = (socket_ == nxINVALID_SOCKET);
    #else
    #if defined(_WIN32)
    const bool is_invalid_socket = (socket_ == static_cast<nxSOCKET>(INVALID_SOCKET));
    #else
    const bool is_invalid_socket = (socket_ == INVALID_SOCKET_VALUE);
    #endif
    #endif

    if (is_invalid_socket) {
        socket_ = INVALID_SOCKET_VALUE;
        ec = make_xnet_error("open");
        return;
    }

    // Set non-blocking mode (required for vsomeip)
    native_non_blocking(true, ec);
    if (ec) {
        const auto its_non_blocking_error = ec;
        boost::system::error_code its_close_error;
        close(its_close_error);
        ec = its_non_blocking_error;
        return;
    }

    VSOMEIP_INFO << "[XNET][udp][open] socket opened protocol=" << (is_ipv6_ ? "IPv6" : "IPv4");
    ec.clear();
}

void xnet_udp_socket::bind(boost::asio::ip::udp::endpoint const& ep, boost::system::error_code& ec) {
    if (!is_open()) { 
        ec = boost::asio::error::bad_descriptor; 
        return; 
    }

    // Validate address family matches the opened socket; reject mismatches early
    const bool ep_is_v6 = ep.address().is_v6();
    if (ep_is_v6 != is_ipv6_) {
        VSOMEIP_ERROR << "[XNET][udp][bind] address family mismatch: socket="
                      << (is_ipv6_ ? "IPv6" : "IPv4")
                      << " endpoint=" << (ep_is_v6 ? "IPv6" : "IPv4");
        ec = boost::asio::error::make_error_code(boost::asio::error::address_family_not_supported);
        return;
    }

    if (is_ipv6_) {
        #if defined(VSOMEIP_ENABLE_XNET)
        nxsockaddr_in6 addr{};
        addr.sin6_family = nxAF_INET6;
        addr.sin6_port = boost::endian::native_to_big(ep.port());
        addr.sin6_flowinfo = 0;
        addr.sin6_scope_id = ep.address().is_v6() ? ep.address().to_v6().scope_id() : 0;

        if (ep.address().is_v6()) {
            auto ipv6_bytes = ep.address().to_v6().to_bytes();
            std::memcpy(addr.sin6_addr.addr, ipv6_bytes.data(), ipv6_bytes.size());
        } else {
            const auto any_v6 = boost::asio::ip::address_v6::any().to_bytes();
            std::memcpy(addr.sin6_addr.addr, any_v6.data(), any_v6.size());
        }

        if (xnet_api::nxbind(socket_, reinterpret_cast<nxsockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("bind");
        } else {
            VSOMEIP_INFO << "[XNET][udp][bind] bound to " << ep.address().to_string() << ":" << ep.port();
            ec.clear();
        }
        #else
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_port = boost::endian::native_to_big(ep.port());
        addr.sin6_flowinfo = 0;
        addr.sin6_scope_id = ep.address().is_v6() ? ep.address().to_v6().scope_id() : 0;
        auto ipv6_bytes = ep.address().is_v6() ? ep.address().to_v6().to_bytes() : boost::asio::ip::address_v6::any().to_bytes();
        std::memcpy(&addr.sin6_addr, ipv6_bytes.data(), ipv6_bytes.size());
        if (::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("bind");
        } else {
            VSOMEIP_INFO << "[XNET][udp][bind] bound to " << ep.address().to_string() << ":" << ep.port();
            ec.clear();
        }
        #endif
    } else {
        #if defined(VSOMEIP_ENABLE_XNET)
        nxsockaddr_in addr{};
        addr.sin_family = nxAF_INET;
        addr.sin_port = boost::endian::native_to_big(ep.port());
        if (ep.address().is_v4()) {
            addr.sin_addr.addr = boost::endian::native_to_big(ep.address().to_v4().to_uint());
        } else {
            addr.sin_addr.addr = boost::endian::native_to_big(boost::asio::ip::address_v4::any().to_uint());
        }

        if (xnet_api::nxbind(socket_, reinterpret_cast<nxsockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("bind");
        } else {
            VSOMEIP_INFO << "[XNET][udp][bind] bound to " << ep.address().to_string() << ":" << ep.port();
            ec.clear();
        }
        #else
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = boost::endian::native_to_big(ep.port());
        addr.sin_addr.s_addr = boost::endian::native_to_big(ep.address().to_v4().to_uint());

        if (::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("bind");
        } else {
            VSOMEIP_INFO << "[XNET][udp][bind] bound to " << ep.address().to_string() << ":" << ep.port();
            ec.clear();
        }
        #endif
    }
}

void xnet_udp_socket::close(boost::system::error_code& ec) { 
    boost::system::error_code its_cancel_error;
    cancel(its_cancel_error);

    // Guarantee that the worker threads are always stopped and joined, even if
    // the backend close call below fails and the function returns early. A
    // joinable std::thread destroyed during socket teardown would otherwise
    // call std::terminate().
    struct worker_stop_guard {
        xnet_udp_socket* self;
        ~worker_stop_guard() { self->stop_worker_threads(); }
    } its_worker_stop_guard{this};

    if (!is_open()) {
        ec.clear();
        return;
    }

    #if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxclose(socket_) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("close");
        return;
    }
    #else
    #if defined(_WIN32)
    if (::closesocket(static_cast<SOCKET>(socket_)) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("close");
        return;
    }
    #else
    if (::close(static_cast<int>(socket_)) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("close");
        return;
    }
    #endif
    #endif

    socket_ = INVALID_SOCKET_VALUE;
    non_blocking_mode_ = false;
    VSOMEIP_INFO << "[XNET][udp][close] socket closed";
    ec.clear();
}

void xnet_udp_socket::cancel(boost::system::error_code& ec) {
    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);
    general_worker_cv_.notify_all();
    receive_worker_cv_.notify_all();
    ec.clear();
}

void xnet_udp_socket::shutdown(boost::asio::ip::udp::socket::shutdown_type st, boost::system::error_code& ec) { 
    if (!is_open()) {
        ec.clear();
        return;
    }

    int how = nxSHUT_RDWR;
    if (st == boost::asio::ip::udp::socket::shutdown_receive) {
        how = nxSHUT_RD;
    } else if (st == boost::asio::ip::udp::socket::shutdown_send) {
        how = nxSHUT_WR;
    }

    #if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxshutdown(socket_, how) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("shutdown");
        return;
    }
    #else
    #if defined(_WIN32)
    if (::shutdown(static_cast<SOCKET>(socket_), how) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("shutdown");
        return;
    }
    #else
    if (::shutdown(static_cast<int>(socket_), how) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("shutdown");
        return;
    }
    #endif
    #endif

    VSOMEIP_INFO << "[XNET][udp][shutdown] type=" << st;
    ec.clear(); 
}

bool xnet_udp_socket::native_non_blocking() const { 
    if (!is_open()) {
        return false;
    }

    #if defined(VSOMEIP_ENABLE_XNET)
    int mode = 0;
    nxsocklen_t opt_len = static_cast<nxsocklen_t>(sizeof(mode));
    if (xnet_api::nxgetsockopt(socket_, nxSOL_SOCKET, nxSO_NONBLOCK, &mode, &opt_len) != SOCKET_ERROR_VALUE
        && opt_len >= static_cast<nxsocklen_t>(sizeof(mode))) {
        return mode != 0;
    }
    return non_blocking_mode_;
    #else
    #if defined(_WIN32)
    return non_blocking_mode_;
    #else
    int flags = ::fcntl(static_cast<int>(socket_), F_GETFL, 0);
    if (flags == SOCKET_ERROR_VALUE) {
        return non_blocking_mode_;
    }
    return (flags & O_NONBLOCK) != 0;
    #endif
    #endif
}

void xnet_udp_socket::native_non_blocking(bool mode, boost::system::error_code& ec) { 
    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    VSOMEIP_INFO << "[XNET][udp][native_non_blocking] set mode=" << mode;
    #if defined(VSOMEIP_ENABLE_XNET)
    int opt = mode ? 1 : 0;
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_NONBLOCK, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("native_non_blocking");
        return;
    }
    #else
    #if defined(_WIN32)
    u_long non_blocking = mode ? 1UL : 0UL;
    if (::ioctlsocket(static_cast<SOCKET>(socket_), FIONBIO, &non_blocking) != 0) {
        ec = make_xnet_error("native_non_blocking");
        return;
    }
    #else
    int flags = ::fcntl(static_cast<int>(socket_), F_GETFL, 0);
    if (flags == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("native_non_blocking");
        return;
    }

    if (mode) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (::fcntl(static_cast<int>(socket_), F_SETFL, flags) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("native_non_blocking");
        return;
    }
    #endif
    #endif

    non_blocking_mode_ = mode;
    ec.clear();
}

void xnet_udp_socket::set_option(boost::asio::ip::udp::socket::reuse_address ra, boost::system::error_code& ec) { 
    VSOMEIP_INFO << "[XNET][udp][set_option:reuse_address] value=" << ra.value();

    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    int opt = ra.value() ? 1 : 0;
    #if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_REUSEADDR, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("set_option:reuse_address");
        return;
    }
    #else
    if (::setsockopt(static_cast<int>(socket_), SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&opt), static_cast<socklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("set_option:reuse_address");
        return;
    }
    #endif

    ec.clear();
}

void xnet_udp_socket::set_option(boost::asio::ip::udp::socket::broadcast broad, boost::system::error_code& ec) { 
    VSOMEIP_INFO << "[XNET][udp][set_option:broadcast] value=" << broad.value();

    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    int opt = broad.value() ? 1 : 0;
    #if defined(VSOMEIP_ENABLE_XNET)
    (void)opt;
    (void)make_unsupported_option_error("set_option:broadcast", "XNET stack does not expose nxSO_BROADCAST");
    ec.clear();
    return;
    #else
    if (::setsockopt(static_cast<int>(socket_), SOL_SOCKET, SO_BROADCAST,
                     reinterpret_cast<const char*>(&opt), static_cast<socklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("set_option:broadcast");
        return;
    }
    ec.clear();
    #endif
}

void xnet_udp_socket::set_option(boost::asio::ip::udp::socket::receive_buffer_size rx_size, boost::system::error_code& ec) { 
    VSOMEIP_INFO << "[XNET][udp][set_option:receive_buffer_size] value=" << rx_size.value();

    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    int opt = rx_size.value();
    #if defined(VSOMEIP_ENABLE_XNET)
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_RCVBUF, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("set_option:receive_buffer_size");
        return;
    }
    #else
    if (::setsockopt(static_cast<int>(socket_), SOL_SOCKET, SO_RCVBUF,
                     reinterpret_cast<const char*>(&opt), static_cast<socklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        ec = make_xnet_error("set_option:receive_buffer_size");
        return;
    }
    #endif

    ec.clear();
}

void xnet_udp_socket::set_option(boost::asio::ip::multicast::join_group join, boost::system::error_code& ec) { 
    VSOMEIP_INFO << "[XNET][udp][set_option:join_group]";

    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    if (is_ipv6_) {
        const auto protocol = boost::asio::ip::udp::v6();
        const auto option_size = join.size(protocol);
        const auto option_data = join.data(protocol);
        if (option_size < sizeof(nxipv6_mreq)) {
            ec = make_family_mismatch_error("set_option:join_group", true);
            return;
        }
        #if defined(VSOMEIP_ENABLE_XNET)
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IPV6, nxIPV6_JOIN_GROUP, option_data, static_cast<nxsocklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:join_group");
            return;
        }
        #else
        if (::setsockopt(static_cast<int>(socket_), IPPROTO_IPV6, IPV6_JOIN_GROUP,
                         reinterpret_cast<const char*>(option_data), static_cast<socklen_t>(option_size)) == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:join_group");
            return;
        }
        #endif
    } else {
        const auto protocol = boost::asio::ip::udp::v4();
        const auto option_size = join.size(protocol);
        const auto option_data = join.data(protocol);
        if (option_size < sizeof(nxip_mreq)) {
            ec = make_family_mismatch_error("set_option:join_group", false);
            return;
        }
        #if defined(VSOMEIP_ENABLE_XNET)
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IP, nxIP_ADD_MEMBERSHIP, option_data, static_cast<nxsocklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:join_group");
            return;
        }
        #else
        if (::setsockopt(static_cast<int>(socket_), IPPROTO_IP, IP_ADD_MEMBERSHIP,
                         reinterpret_cast<const char*>(option_data), static_cast<socklen_t>(option_size)) == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:join_group");
            return;
        }
        #endif
    }

    ec.clear();
}

void xnet_udp_socket::set_option(boost::asio::ip::multicast::leave_group leave, boost::system::error_code& ec) { 
    VSOMEIP_INFO << "[XNET][udp][set_option:leave_group]";

    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    if (is_ipv6_) {
        const auto protocol = boost::asio::ip::udp::v6();
        const auto option_size = leave.size(protocol);
        const auto option_data = leave.data(protocol);
        if (option_size < sizeof(nxipv6_mreq)) {
            ec = make_family_mismatch_error("set_option:leave_group", true);
            return;
        }
        #if defined(VSOMEIP_ENABLE_XNET)
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IPV6, nxIPV6_LEAVE_GROUP, option_data, static_cast<nxsocklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:leave_group");
            return;
        }
        #else
        if (::setsockopt(static_cast<int>(socket_), IPPROTO_IPV6, IPV6_LEAVE_GROUP,
                         reinterpret_cast<const char*>(option_data), static_cast<socklen_t>(option_size)) == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:leave_group");
            return;
        }
        #endif
    } else {
        const auto protocol = boost::asio::ip::udp::v4();
        const auto option_size = leave.size(protocol);
        const auto option_data = leave.data(protocol);
        if (option_size < sizeof(nxip_mreq)) {
            ec = make_family_mismatch_error("set_option:leave_group", false);
            return;
        }
        #if defined(VSOMEIP_ENABLE_XNET)
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IP, nxIP_DROP_MEMBERSHIP, option_data, static_cast<nxsocklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:leave_group");
            return;
        }
        #else
        if (::setsockopt(static_cast<int>(socket_), IPPROTO_IP, IP_DROP_MEMBERSHIP,
                         reinterpret_cast<const char*>(option_data), static_cast<socklen_t>(option_size)) == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:leave_group");
            return;
        }
        #endif
    }

    ec.clear();
}

void xnet_udp_socket::set_option(boost::asio::ip::multicast::outbound_interface outbound, boost::system::error_code& ec) { 
    VSOMEIP_INFO << "[XNET][udp][set_option:outbound_interface]";

    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    if (is_ipv6_) {
        const auto protocol = boost::asio::ip::udp::v6();
        const auto option_size = outbound.size(protocol);
        if (option_size != sizeof(unsigned int)) {
            ec = make_family_mismatch_error("set_option:outbound_interface", true);
            return;
        }
        #if defined(VSOMEIP_ENABLE_XNET)
        int32_t if_index = static_cast<int32_t>(*reinterpret_cast<const unsigned int*>(outbound.data(protocol)));
        if (if_index == 0) {
            nxVirtualInterface_t* interfaces = nullptr;
            const auto status = ::nxIpStackGetInfo(xnet_stack_, nxIPSTACK_INFO_ID, &interfaces);
            if (status == 0) {
                for (auto* interface = interfaces; interface != nullptr; interface = interface->nextVirtualInterface) {
                    if (interface->operationalStatus == nxOPERATIONAL_STATUS_UP) {
                        if_index = static_cast<int32_t>(interface->ifIndex);
                        break;
                    }
                }
                ::nxIpStackFreeInfo(interfaces);
            }
        }
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IPV6, nxIPV6_MULTICAST_IF, &if_index, static_cast<nxsocklen_t>(sizeof(if_index)))
            == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:outbound_interface");
            return;
        }
        #else
        if (::setsockopt(static_cast<int>(socket_), IPPROTO_IPV6, IPV6_MULTICAST_IF,
                         reinterpret_cast<const char*>(outbound.data(protocol)), static_cast<socklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:outbound_interface");
            return;
        }
        #endif
    } else {
        const auto protocol = boost::asio::ip::udp::v4();
        const auto option_size = outbound.size(protocol);
        if (option_size != sizeof(nxin_addr)) {
            ec = make_family_mismatch_error("set_option:outbound_interface", false);
            return;
        }
        #if defined(VSOMEIP_ENABLE_XNET)
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IP, nxIP_MULTICAST_IF, outbound.data(protocol), static_cast<nxsocklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:outbound_interface");
            return;
        }
        #else
        if (::setsockopt(static_cast<int>(socket_), IPPROTO_IP, IP_MULTICAST_IF,
                         reinterpret_cast<const char*>(outbound.data(protocol)), static_cast<socklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = make_xnet_error("set_option:outbound_interface");
            return;
        }
        #endif
    }

    ec.clear();
}

#if defined(__linux__) || defined(__QNX__)
void xnet_udp_socket::set_option([[maybe_unused]] udp_bind_to_device _opt, boost::system::error_code& _ec) {
    VSOMEIP_INFO << "[XNET][udp][set_option:bind_to_device]";
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    #if defined(VSOMEIP_ENABLE_XNET)
    // SO_BINDTODEVICE is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
    #else
    const auto protocol = boost::asio::ip::udp::v4();
    if (::setsockopt(static_cast<int>(socket_), _opt.level(protocol), _opt.name(protocol),
                     _opt.data(protocol), static_cast<socklen_t>(_opt.size(protocol))) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("set_option:bind_to_device");
        return;
    }
    _ec.clear();
    #endif
}

void xnet_udp_socket::set_option([[maybe_unused]] udp_packet_info_ip4 _opt, boost::system::error_code& _ec) {
    VSOMEIP_INFO << "[XNET][udp][set_option:packet_info_ip4]";
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    #if defined(VSOMEIP_ENABLE_XNET)
    // IP_PKTINFO is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
    #else
    const auto protocol = boost::asio::ip::udp::v4();
    if (::setsockopt(static_cast<int>(socket_), _opt.level(protocol), _opt.name(protocol),
                     _opt.data(protocol), static_cast<socklen_t>(_opt.size(protocol))) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("set_option:packet_info_ip4");
        return;
    }
    _ec.clear();
    #endif
}

void xnet_udp_socket::set_option([[maybe_unused]] udp_packet_info_ip6 _opt, boost::system::error_code& _ec) {
    VSOMEIP_INFO << "[XNET][udp][set_option:packet_info_ip6]";
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    #if defined(VSOMEIP_ENABLE_XNET)
    // IPV6_RECVPKTINFO is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
    #else
    const auto protocol = boost::asio::ip::udp::v6();
    if (::setsockopt(static_cast<int>(socket_), _opt.level(protocol), _opt.name(protocol),
                     _opt.data(protocol), static_cast<socklen_t>(_opt.size(protocol))) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("set_option:packet_info_ip6");
        return;
    }
    _ec.clear();
    #endif
}

void xnet_udp_socket::set_option([[maybe_unused]] udp_send_timeout _opt, boost::system::error_code& _ec) {
    VSOMEIP_INFO << "[XNET][udp][set_option:send_timeout]";
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    #if defined(VSOMEIP_ENABLE_XNET)
    // SO_SNDTIMEO is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
    #else
    const auto protocol = boost::asio::ip::udp::v4();
    if (::setsockopt(static_cast<int>(socket_), _opt.level(protocol), _opt.name(protocol),
                     _opt.data(protocol), static_cast<socklen_t>(_opt.size(protocol))) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("set_option:send_timeout");
        return;
    }
    _ec.clear();
    #endif
}

void xnet_udp_socket::set_option([[maybe_unused]] udp_receive_timeout _opt, boost::system::error_code& _ec) {
    VSOMEIP_INFO << "[XNET][udp][set_option:receive_timeout]";
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    #if defined(VSOMEIP_ENABLE_XNET)
    // SO_RCVTIMEO is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
    #else
    const auto protocol = boost::asio::ip::udp::v4();
    if (::setsockopt(static_cast<int>(socket_), _opt.level(protocol), _opt.name(protocol),
                     _opt.data(protocol), static_cast<socklen_t>(_opt.size(protocol))) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("set_option:receive_timeout");
        return;
    }
    _ec.clear();
    #endif
}

bool xnet_udp_socket::can_read_fd_flags() {
    #if defined(VSOMEIP_ENABLE_XNET)
    return false;
    #else
    return true;
    #endif
}
#endif // defined(__linux__) || defined(__QNX__)

#ifdef __linux__
void xnet_udp_socket::set_option([[maybe_unused]] udp_receive_buffer_force _opt, boost::system::error_code& _ec) {
    VSOMEIP_INFO << "[XNET][udp][set_option:receive_buffer_force]";
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    #if defined(VSOMEIP_ENABLE_XNET)
    // SO_RCVBUFFORCE is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
    #else
    const auto protocol = boost::asio::ip::udp::v4();
    if (::setsockopt(static_cast<int>(socket_), _opt.level(protocol), _opt.name(protocol),
                     _opt.data(protocol), static_cast<socklen_t>(_opt.size(protocol))) == SOCKET_ERROR_VALUE) {
        _ec = make_xnet_error("set_option:receive_buffer_force");
        return;
    }
    _ec.clear();
    #endif
}
#endif // __linux__

void xnet_udp_socket::get_option(boost::asio::ip::udp::socket::receive_buffer_size& rx_size, boost::system::error_code& ec) {
    VSOMEIP_INFO << "[XNET][udp][get_option:receive_buffer_size]";

    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    int value = 0;
    #if defined(VSOMEIP_ENABLE_XNET)
    nxsocklen_t opt_len = static_cast<nxsocklen_t>(sizeof(value));
    if (xnet_api::nxgetsockopt(socket_, nxSOL_SOCKET, nxSO_RCVBUF, &value, &opt_len) == SOCKET_ERROR_VALUE || opt_len < static_cast<nxsocklen_t>(sizeof(value))) {
        ec = make_xnet_error("get_option:receive_buffer_size");
        return;
    }
    #else
    socklen_t opt_len = static_cast<socklen_t>(sizeof(value));
    if (::getsockopt(static_cast<int>(socket_), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&value), &opt_len)
        == SOCKET_ERROR_VALUE || opt_len < sizeof(value)) {
        ec = make_xnet_error("get_option:receive_buffer_size");
        return;
    }
    #endif

    rx_size = boost::asio::ip::udp::socket::receive_buffer_size(value);
    ec.clear();
}

boost::asio::ip::udp::endpoint xnet_udp_socket::local_endpoint(boost::system::error_code& ec) const {
    VSOMEIP_INFO << "[XNET][udp][local_endpoint] query";
    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
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
        ec = make_xnet_error("local_endpoint");
        return {};
    }

    boost::asio::ip::udp::endpoint its_endpoint;
    if (!native_to_endpoint(its_storage, its_len, its_endpoint, ec)) {
        return {};
    }

    ec.clear();
    return its_endpoint;
}

void xnet_udp_socket::async_connect(boost::asio::ip::udp::endpoint const& remote, completion_handler handler) {
    VSOMEIP_INFO << "[XNET][udp][async_connect] remote=" << remote.address().to_string() << ":" << remote.port();

    if (!is_open()) {
        post_completion(io_context_, std::move(handler), boost::asio::error::bad_descriptor);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto* its_io = &io_context_;
    if (!enqueue_work([this, its_io, its_socket, its_epoch, remote, handler = std::move(handler)]() mutable {
        boost::system::error_code its_error;

        nxsockaddr_storage its_storage{};
        nxsocklen_t its_length = 0;
        if (!endpoint_to_native(remote, its_storage, its_length, its_error)) {
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
                if (wait_socket_ready(its_socket, boost::asio::ip::udp::socket::wait_write,
                                      stop_requested_, cancel_epoch_, its_epoch, its_wait_error)) {
                    its_error.clear();
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
        post_completion(io_context_, std::move(handler), boost::asio::error::operation_aborted);
    }
}

void xnet_udp_socket::async_receive_from(boost::asio::mutable_buffer b, boost::asio::ip::udp::endpoint& remote, rw_handler handler) {
    if (!is_open()) {
        post_rw_completion(io_context_, std::move(handler), boost::asio::error::bad_descriptor, 0);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto* const its_remote = &remote;
    auto its_receive_data = std::make_shared<std::vector<std::uint8_t>>(b.size());
    auto its_source_endpoint = std::make_shared<boost::asio::ip::udp::endpoint>();
    const auto its_buffer_size = static_cast<int32_t>(clamp_size_to_int32(its_receive_data->size()));

    auto* its_io = &io_context_;
    if (!enqueue_receive_work([this, its_io, its_socket, its_epoch, caller_buffer = b, its_remote, its_receive_data,
                               its_source_endpoint, its_buffer_size, handler = std::move(handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t its_bytes_received = 0;

        for (;;) {
            nxsockaddr_storage its_from_storage{};
            nxsocklen_t its_from_len = static_cast<nxsocklen_t>(sizeof(its_from_storage));
            auto* const its_receive_ptr = its_receive_data->empty() ? nullptr : its_receive_data->data();

#if defined(VSOMEIP_ENABLE_XNET)
            const auto its_result =
                xnet_api::nxrecvfrom(its_socket, its_receive_ptr, its_buffer_size, 0, reinterpret_cast<nxsockaddr*>(&its_from_storage), &its_from_len);
#else
            auto its_result =
                ::recvfrom(static_cast<int>(its_socket), reinterpret_cast<char*>(its_receive_ptr), its_buffer_size, 0,
                           reinterpret_cast<sockaddr*>(&its_from_storage), reinterpret_cast<socklen_t*>(&its_from_len));
#endif

            if (its_result >= 0) {
                its_bytes_received = static_cast<std::size_t>(its_result);
                if (!native_to_endpoint(its_from_storage, its_from_len, *its_source_endpoint, its_error)) {
                    its_bytes_received = 0;
                }
                break;
            }

            its_error = make_xnet_error("async_receive_from");
            if (!is_would_block_like(its_error)) {
                break;
            }

            boost::system::error_code its_wait_error;
            if (!wait_socket_ready(its_socket, boost::asio::ip::udp::socket::wait_read,
                                   stop_requested_, cancel_epoch_, its_epoch, its_wait_error)) {
                its_error = its_wait_error;
                break;
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
            its_bytes_received = 0;
        } else if (its_error == boost::asio::error::bad_descriptor) {
            // Socket teardown race on close/rejoin can surface as invalid descriptor
            // from nxrecvfrom/nxselect; treat it as a deterministic cancellation.
            its_error = boost::asio::error::operation_aborted;
            its_bytes_received = 0;
        }

        post_receive_from_completion(*its_io, std::move(handler), its_error, its_bytes_received,
                                     caller_buffer, its_remote, its_receive_data, its_source_endpoint);
    })) {
        post_rw_completion(io_context_, std::move(handler), boost::asio::error::operation_aborted, 0);
    }
}

void xnet_udp_socket::async_send(boost::asio::const_buffer const& b, rw_handler handler) {
    VSOMEIP_INFO << "[XNET][udp][async_send] bytes=" << b.size();

    if (!is_open()) {
        post_rw_completion(io_context_, std::move(handler), boost::asio::error::bad_descriptor, 0);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto its_buffer_data = std::make_shared<std::vector<std::uint8_t>>();
    auto const* its_buffer_begin = static_cast<const std::uint8_t*>(b.data());
    if (b.size() > 0) {
        if (its_buffer_begin == nullptr) {
            post_rw_completion(io_context_, std::move(handler), boost::asio::error::invalid_argument, 0);
            return;
        }
        its_buffer_data->assign(its_buffer_begin, its_buffer_begin + b.size());
    }
    const auto its_buffer_size = static_cast<int32_t>(clamp_size_to_int32(its_buffer_data->size()));

    auto* its_io = &io_context_;
    if (!enqueue_work([this, its_io, its_socket, its_epoch, its_buffer_data, its_buffer_size, handler = std::move(handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t its_bytes_sent = 0;

        for (;;) {
#if defined(VSOMEIP_ENABLE_XNET)
            const auto its_result = xnet_api::nxsend(its_socket, its_buffer_data->data(), its_buffer_size, 0);
#else
            const auto its_result =
                ::send(static_cast<int>(its_socket), reinterpret_cast<const char*>(its_buffer_data->data()), its_buffer_size, 0);
#endif

            if (its_result >= 0) {
                its_bytes_sent = static_cast<std::size_t>(its_result);
                its_error.clear();
                break;
            }

            its_error = make_xnet_error("async_send");
            if (!is_would_block_like(its_error)) {
                break;
            }

            boost::system::error_code its_wait_error;
            if (!wait_socket_ready(its_socket, boost::asio::ip::udp::socket::wait_write,
                                   stop_requested_, cancel_epoch_, its_epoch, its_wait_error)) {
                its_error = its_wait_error;
                break;
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
            its_bytes_sent = 0;
        }

        post_rw_completion(*its_io, std::move(handler), its_error, its_bytes_sent);
    })) {
        post_rw_completion(io_context_, std::move(handler), boost::asio::error::operation_aborted, 0);
    }
}

void xnet_udp_socket::async_send_to(boost::asio::const_buffer const& b, boost::asio::ip::udp::endpoint destination, rw_handler handler) {
    VSOMEIP_INFO << "[XNET][udp][async_send_to] bytes=" << b.size()
                 << " destination=" << destination.address().to_string() << ":" << destination.port();

    if (!is_open()) {
        post_rw_completion(io_context_, std::move(handler), boost::asio::error::bad_descriptor, 0);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto its_buffer_data = std::make_shared<std::vector<std::uint8_t>>();
    auto const* its_buffer_begin = static_cast<const std::uint8_t*>(b.data());
    if (b.size() > 0) {
        if (its_buffer_begin == nullptr) {
            post_rw_completion(io_context_, std::move(handler), boost::asio::error::invalid_argument, 0);
            return;
        }
        its_buffer_data->assign(its_buffer_begin, its_buffer_begin + b.size());
    }
    const auto its_buffer_size = static_cast<int32_t>(clamp_size_to_int32(its_buffer_data->size()));

    auto* its_io = &io_context_;
    if (!enqueue_work([this, its_io, its_socket, its_epoch, destination, its_buffer_data, its_buffer_size, handler = std::move(handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t its_bytes_sent = 0;

        nxsockaddr_storage its_destination_storage{};
        nxsocklen_t its_destination_length = 0;
        if (!endpoint_to_native(destination, its_destination_storage, its_destination_length, its_error)) {
            post_rw_completion(*its_io, std::move(handler), its_error, 0);
            return;
        }

        for (;;) {
#if defined(VSOMEIP_ENABLE_XNET)
            const auto its_result = xnet_api::nxsendto(its_socket, its_buffer_data->data(), its_buffer_size, 0,
                                             reinterpret_cast<nxsockaddr*>(&its_destination_storage), its_destination_length);
#else
            const auto its_result =
                ::sendto(static_cast<int>(its_socket), reinterpret_cast<const char*>(its_buffer_data->data()), its_buffer_size, 0,
                         reinterpret_cast<sockaddr*>(&its_destination_storage), static_cast<socklen_t>(its_destination_length));
#endif

            if (its_result >= 0) {
                its_bytes_sent = static_cast<std::size_t>(its_result);
                its_error.clear();
                break;
            }

            its_error = make_xnet_error("async_send_to");
            if (!is_would_block_like(its_error)) {
                break;
            }

            boost::system::error_code its_wait_error;
            if (!wait_socket_ready(its_socket, boost::asio::ip::udp::socket::wait_write,
                                   stop_requested_, cancel_epoch_, its_epoch, its_wait_error)) {
                its_error = its_wait_error;
                break;
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
            its_bytes_sent = 0;
        }

        post_rw_completion(*its_io, std::move(handler), its_error, its_bytes_sent);
    })) {
        post_rw_completion(io_context_, std::move(handler), boost::asio::error::operation_aborted, 0);
    }
}

void xnet_udp_socket::async_wait(boost::asio::ip::udp::socket::wait_type wait, completion_handler handler) {
    VSOMEIP_INFO << "[XNET][udp][async_wait] wait="
                 << (wait == boost::asio::ip::udp::socket::wait_read ? "read"
                 : (wait == boost::asio::ip::udp::socket::wait_write ? "write" : "error"));

    if (!is_open()) {
        post_completion(io_context_, std::move(handler), boost::asio::error::bad_descriptor);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto* its_io = &io_context_;
    if (!enqueue_work([this, its_io, its_socket, its_epoch, wait, handler = std::move(handler)]() mutable {
        boost::system::error_code its_error;
        (void)wait_socket_ready(its_socket, wait, stop_requested_, cancel_epoch_, its_epoch, its_error);
        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
        }
        post_completion(*its_io, std::move(handler), its_error);
    })) {
        post_completion(io_context_, std::move(handler), boost::asio::error::operation_aborted);
    }

}

bool xnet_udp_socket::is_canceled(std::uint64_t _epoch) const {
    return cancel_epoch_.load(std::memory_order_relaxed) != _epoch;
}

}

