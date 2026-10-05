#include <cstring>

#include <boost/asio/post.hpp>
#include <boost/asio/error.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/asio/associated_executor.hpp>

#include "../include/xnet_api.hpp"
#include "../include/xnet_error.hpp"
#include "../include/xnet_socket_helper.hpp"

#include "logger_ext.hpp"

#define VSOMEIP_LOG_PREFIX log_prefix

namespace vsomeip_v3 {

namespace xnet_socket_helper {

// Default timeout for nxselect() when no timeout is specified
constexpr int SELECT_POLL_TIMEOUT_MS = 200;

boost::system::error_code get_xnet_error() {
    return xnet_to_boost_error(xnet_get_last_error());
}

boost::system::error_code make_unsupported_option_warning(char const* log_prefix, char const* _option, char const* _reason) {
    VSOMEIP_WARNING_P << "option=" << _option << " unsupported: " << _reason;
    return boost::asio::error::make_error_code(boost::asio::error::operation_not_supported);
}

void post_completion(boost::asio::io_context& _io, connect_handler _handler, boost::system::error_code const& _ec) {
    auto its_executor = boost::asio::get_associated_executor(_handler, _io.get_executor());
    boost::asio::post(its_executor, [handler = std::move(_handler), ec = _ec]() mutable { handler(ec); });
}

void post_rw_completion(boost::asio::io_context& _io, rw_handler _handler, boost::system::error_code const& _ec, std::size_t _bytes) {
    auto its_executor = boost::asio::get_associated_executor(_handler, _io.get_executor());
    boost::asio::post(its_executor, [handler = std::move(_handler), ec = _ec, bytes = _bytes]() mutable { handler(ec, bytes); });
}

bool wait_socket_ready(nxSOCKET _socket, std::optional<std::chrono::milliseconds> _timeout, boost::asio::socket_base::wait_type _wait_type,
                       std::atomic<std::uint64_t> const& _cancel_epoch, std::uint64_t _operation_epoch, boost::system::error_code& _ec) {
    // Initialize timeout values
    const std::chrono::milliseconds::rep timeout_ms = _timeout ? _timeout->count() : SELECT_POLL_TIMEOUT_MS;

    if (timeout_ms < 0 || timeout_ms > std::numeric_limits<int>::max()) {
        _ec = boost::asio::error::make_error_code(boost::asio::error::invalid_argument);
        return false;
    }

    const auto timeout_sec = static_cast<long>(timeout_ms / 1000);
    const auto timeout_usec = static_cast<long>((timeout_ms % 1000) * 1000);

    nxfd_set read_fds{};
    nxfd_set write_fds{};
    nxfd_set except_fds{};

    for (;;) {
        // Initialize file descriptor sets based on the wait type
        nxFD_ZERO(&read_fds);
        nxFD_ZERO(&write_fds);
        nxFD_ZERO(&except_fds);

        if (_wait_type == boost::asio::socket_base::wait_read) {
            nxFD_SET(_socket, &read_fds);
        } else if (_wait_type == boost::asio::socket_base::wait_write) {
            nxFD_SET(_socket, &write_fds);
        } else {
            nxFD_SET(_socket, &except_fds);
        }

        nxtimeval timeout{};
        timeout.tv_sec = timeout_sec;
        timeout.tv_usec = static_cast<int32_t>(timeout_usec);

        // Wait for the socket to be ready
        const auto its_result = xnet_api::nxselect(0, &read_fds, &write_fds, &except_fds, &timeout);

        if (its_result > 0) {
            // Socket is ready
            _ec.clear();
            return true;
        }

        if (its_result == 0) {
            // Timeout occurred
            if (_cancel_epoch.load(std::memory_order_relaxed) != _operation_epoch) {
                _ec = boost::asio::error::operation_aborted;
                return false;
            }

            // If no timeout was specified, repeat the operation
            if (_timeout == std::nullopt) {
                continue;
            }
            _ec.clear();
            return false;
        }

        // An error occurred
        _ec = get_xnet_error();
        if (_ec == boost::asio::error::interrupted) {
            continue;
        }
        return false;
    }
}

bool endpoint_to_native(const boost::asio::ip::address& endpoint_address, const unsigned short& endpoint_port,
                        nxsockaddr_storage& _storage, nxsocklen_t& _len, boost::system::error_code& _ec) {
    std::memset(&_storage, 0, sizeof(_storage));
    if (endpoint_address.is_v4()) {
        auto* its_addr = reinterpret_cast<nxsockaddr_in*>(&_storage);
        its_addr->sin_family = nxAF_INET;
        its_addr->sin_port = boost::endian::native_to_big(endpoint_port);
        const auto its_v4 = boost::endian::native_to_big(endpoint_address.to_v4().to_uint());
        std::memcpy(&its_addr->sin_addr, &its_v4, sizeof(its_v4));
        _len = static_cast<nxsocklen_t>(sizeof(nxsockaddr_in));
    } 
    else if (endpoint_address.is_v6()) {
        auto* its_addr = reinterpret_cast<nxsockaddr_in6*>(&_storage);
        its_addr->sin6_family = nxAF_INET6;
        its_addr->sin6_port = boost::endian::native_to_big(endpoint_port);
        its_addr->sin6_flowinfo = 0;
        its_addr->sin6_scope_id = endpoint_address.to_v6().scope_id();
        const auto its_bytes = endpoint_address.to_v6().to_bytes();
        std::memcpy(&its_addr->sin6_addr, its_bytes.data(), its_bytes.size());
        _len = static_cast<nxsocklen_t>(sizeof(nxsockaddr_in6));
    }
    else {
        _ec = boost::asio::error::make_error_code(boost::asio::error::address_family_not_supported);
        return false;
    }

    _ec.clear();
    return true;
}

bool native_to_endpoint(nxsockaddr_storage const& _storage, nxsocklen_t _len, boost::asio::ip::address& endpoint_address,
                        unsigned short& endpoint_port, boost::system::error_code& _ec) {
    const auto* its_sockaddr = reinterpret_cast<const nxsockaddr*>(&_storage);
    if (its_sockaddr->sa_family == nxAF_INET && static_cast<std::size_t>(_len) >= sizeof(nxsockaddr_in)) {
        const auto* its_addr = reinterpret_cast<const nxsockaddr_in*>(&_storage);
        std::uint32_t its_raw_v4 = 0;
        std::memcpy(&its_raw_v4, &its_addr->sin_addr, sizeof(its_raw_v4));
        endpoint_address = boost::asio::ip::address_v4(boost::endian::big_to_native(its_raw_v4));
        endpoint_port = boost::endian::big_to_native(its_addr->sin_port);
    }
    else if (its_sockaddr->sa_family == nxAF_INET6 && static_cast<std::size_t>(_len) >= sizeof(nxsockaddr_in6)) {
        const auto* its_addr = reinterpret_cast<const nxsockaddr_in6*>(&_storage);
        boost::asio::ip::address_v6::bytes_type its_bytes{};
        std::memcpy(its_bytes.data(), &its_addr->sin6_addr, its_bytes.size());
        endpoint_address = boost::asio::ip::address_v6(its_bytes, its_addr->sin6_scope_id);
        endpoint_port = boost::endian::big_to_native(its_addr->sin6_port);
    }
    else {
        _ec = boost::asio::error::make_error_code(boost::asio::error::address_family_not_supported);
        return false;
    }

    _ec.clear();
    return true;
}

worker_thread::worker_thread() : stop_requested_(false) { }

worker_thread::~worker_thread() {
    request_stop();
    join();
}

void worker_thread::ensure_started() {
    std::lock_guard<std::mutex> its_lock(mutex_);
    if (thread_.joinable()) {
        return;
    }

    stop_requested_.store(false, std::memory_order_relaxed);
    thread_ = std::thread([this]() { loop(); });
}

bool worker_thread::enqueue(work_item_t&& _item) {
    ensure_started();
    {
        std::lock_guard<std::mutex> its_lock(mutex_);
        if (stop_requested_.load(std::memory_order_relaxed)) {
            return false;
        }
        queue_.emplace_back(std::move(_item));
    }
    cv_.notify_one();
    return true;
}

void worker_thread::request_stop() {
    {
        std::lock_guard<std::mutex> its_lock(mutex_);
        stop_requested_.store(true, std::memory_order_relaxed);
    }
    cv_.notify_all();
}

void worker_thread::join() {
    if (thread_.joinable()) {
        thread_.join();
    }

    std::lock_guard<std::mutex> its_lock(mutex_);
    queue_.clear();
}

void worker_thread::loop() {
    for (;;) {
        work_item_t its_item;
        {
            std::unique_lock<std::mutex> its_lock(mutex_);
            cv_.wait(its_lock, [this]() { return stop_requested_.load(std::memory_order_relaxed) || !queue_.empty(); });

            if (stop_requested_.load(std::memory_order_relaxed) && queue_.empty()) {
                return;
            }

            its_item = std::move(queue_.front());
            queue_.pop_front();
        }

        if (its_item) {
            its_item();
        }
    }
}

}

}
