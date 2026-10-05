#pragma once

#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <algorithm>
#include <functional>
#include <condition_variable>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/system/error_code.hpp>

#include "nxsocket.h"

namespace vsomeip_v3 {

namespace xnet_socket_helper {

using connect_handler = std::function<void(boost::system::error_code const&)>;
using rw_handler = std::function<void(boost::system::error_code const&, size_t)>;

// Convert the last error from xnet to a Boost.Asio error code
boost::system::error_code get_xnet_error();

// Log a warning message for an unsupported option and return an appropriate error code
boost::system::error_code make_unsupported_option_warning(char const* log_prefix, char const* _option, char const* _reason);

// Post completion handler to the associated executor of the handler
void post_completion(boost::asio::io_context& _io, connect_handler _handler, boost::system::error_code const& _ec);

// Post a read/write completion handler to the associated executor of the handler
void post_rw_completion(boost::asio::io_context& _io, rw_handler _handler, boost::system::error_code const& _ec, std::size_t _bytes);

// Wait for a socket to be ready for reading or writing
bool wait_socket_ready(nxSOCKET _socket, std::optional<std::chrono::milliseconds> _timeout, boost::asio::socket_base::wait_type _wait_type,
                       std::atomic<std::uint64_t> const& _cancel_epoch, std::uint64_t _operation_epoch, boost::system::error_code& _ec);

// Convert a Boost.Asio endpoint to a native sockaddr structure
bool endpoint_to_native(const boost::asio::ip::address& endpoint_address, const unsigned short& endpoint_port, nxsockaddr_storage& _storage,
                        nxsocklen_t& _len, boost::system::error_code& _ec);

// Convert a native sockaddr structure to a Boost.Asio endpoint
bool native_to_endpoint(nxsockaddr_storage const& _storage, nxsocklen_t _len, boost::asio::ip::address& endpoint_address,
                        unsigned short& endpoint_port, boost::system::error_code& _ec);

// Clamp a size value to the maximum value of int32_t
inline std::size_t clamp_size_to_int32(std::size_t _size) {
    return (std::min)(_size, static_cast<std::size_t>((std::numeric_limits<int32_t>::max)()));
}

// Check if an error code is similar to a "would block" condition
inline bool is_would_block_like(boost::system::error_code const& _ec) {
    return _ec == boost::asio::error::would_block || _ec == boost::asio::error::try_again || _ec == boost::asio::error::in_progress;
}


class worker_thread {
public:
    using work_item_t = std::function<void()>;

    worker_thread();

    ~worker_thread();

    // Enqueue a work item to the queue.
    bool enqueue(work_item_t&& _item);

    // Signal the worker to stop
    void request_stop();

    // Join the worker thread
    void join();

private:
    // Ensure the worker thread is started
    void ensure_started();

    // The main loop of the worker thread that processes work items from the queue.
    void loop();

    std::thread thread_;
    std::atomic<bool> stop_requested_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<work_item_t> queue_;
};

}

}
