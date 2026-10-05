#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <optional>

#include <boost/asio/buffer.hpp>

#include "../include/xnet_socket_helper.hpp"
#include "../include/xnet_tcp_socket.hpp"
#include "../include/xnet_error.hpp"
#include "../include/xnet_api.hpp"

#include "logger_ext.hpp"

#define VSOMEIP_LOG_PREFIX "[XNET][tcp]"

#define SOCKET_ERROR_VALUE -1

namespace vsomeip_v3 {

xnet_tcp_socket::xnet_tcp_socket(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack)
    : socket_(nxINVALID_SOCKET),
      io_context_(_io),
      xnet_stack_(_xnet_stack),
      is_ipv6_(false),
      cancel_epoch_(0) {
}

xnet_tcp_socket::~xnet_tcp_socket() {
    boost::system::error_code ec;
    close(ec);
}

bool xnet_tcp_socket::is_open() const {
    return socket_ != nxINVALID_SOCKET;
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

    int opt = 0;
    if (xnet_api::nxsetsockopt(its_socket, nxSOL_SOCKET, nxSO_NONBLOCK, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        _ec = xnet_socket_helper::get_xnet_error();
        return;
    }

    socket_ = its_socket;
    is_ipv6_ = _is_ipv6;
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

    socket_ = xnet_api::nxsocket(xnet_stack_, is_ipv6_ ? nxAF_INET6 : nxAF_INET, nxSOCK_STREAM, nxIPPROTO_TCP);

    if (socket_ == nxINVALID_SOCKET) {
        _ec = xnet_socket_helper::get_xnet_error();
        return;
    }

    int opt = 0;
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_NONBLOCK, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        _ec = xnet_socket_helper::get_xnet_error();
        boost::system::error_code its_close_error;
        close(its_close_error);
        return;
    }

    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);
    _ec.clear();
}

void xnet_tcp_socket::bind(boost::asio::ip::tcp::endpoint const& _ep, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    nxsockaddr_storage its_storage{};
    nxsocklen_t its_len = 0;
    if (!xnet_socket_helper::endpoint_to_native(_ep.address(), _ep.port(), its_storage, its_len, _ec)) {
        return;
    }

    if (xnet_api::nxbind(socket_, reinterpret_cast<nxsockaddr*>(&its_storage), its_len) == SOCKET_ERROR_VALUE) {
        _ec = xnet_socket_helper::get_xnet_error();
        return;
    }

    _ec.clear();
}

void xnet_tcp_socket::close(boost::system::error_code& _ec) {
    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);

    if (is_open()) {
        if (xnet_api::nxclose(socket_) == SOCKET_ERROR_VALUE) {
            _ec = xnet_socket_helper::get_xnet_error();
            socket_ = nxINVALID_SOCKET;
            stop_worker_threads();
            return;
        }
        socket_ = nxINVALID_SOCKET;
    }
    _ec.clear();
    stop_worker_threads();
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

    if (xnet_api::nxgetsockname(socket_, reinterpret_cast<nxsockaddr*>(&its_storage), &its_len) == SOCKET_ERROR_VALUE) {
        _ec = xnet_socket_helper::get_xnet_error();
        return {};
    }

    boost::asio::ip::address endpoint_address;
    boost::asio::ip::port_type endpoint_port;
    if (!xnet_socket_helper::native_to_endpoint(its_storage, its_len, endpoint_address, endpoint_port, _ec)) {
        return {};
    }

    _ec.clear();
    return boost::asio::ip::tcp::endpoint(endpoint_address, endpoint_port);
}

void xnet_tcp_socket::io_control(io_control_operation<std::size_t>& _icm, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    int32_t rx_data = 0;
    nxsocklen_t opt_len = static_cast<nxsocklen_t>(sizeof(rx_data));
    if (xnet_api::nxgetsockopt(socket_, nxSOL_SOCKET, nxSO_RXDATA, &rx_data, &opt_len) == SOCKET_ERROR_VALUE
        || opt_len < static_cast<nxsocklen_t>(sizeof(rx_data))) {
        _ec = xnet_socket_helper::get_xnet_error();
        return;
    }
    _icm.set(static_cast<std::size_t>(rx_data < 0 ? 0 : rx_data));
    _ec.clear();
}

void xnet_tcp_socket::set_option(boost::asio::ip::tcp::no_delay _nd, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    int opt = _nd.value() ? 1 : 0;
    if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_TCP, nxTCP_NODELAY, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        _ec = xnet_socket_helper::get_xnet_error();
        return;
    }

    _ec.clear();
}

void xnet_tcp_socket::set_option(boost::asio::ip::tcp::socket::keep_alive _ka, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    _ec = xnet_socket_helper::make_unsupported_option_warning(VSOMEIP_LOG_PREFIX, "keep_alive",
                                        _ka.value() ? "enable keepalive is not supported by XNET socket API"
                                                    : "disable keepalive is not supported by XNET socket API");
}

void xnet_tcp_socket::set_option(boost::asio::ip::tcp::socket::linger _l, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    nxlinger linger_opt{};
    linger_opt.l_onoff = _l.enabled() ? 1 : 0;
    linger_opt.l_linger = _l.timeout();
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_LINGER, &linger_opt, static_cast<nxsocklen_t>(sizeof(linger_opt)))
        == SOCKET_ERROR_VALUE) {
        _ec = xnet_socket_helper::get_xnet_error();
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
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_REUSEADDR, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        _ec = xnet_socket_helper::get_xnet_error();
        return;
    }

    _ec.clear();
}

void xnet_tcp_socket::async_connect(boost::asio::ip::tcp::endpoint const& _ep, connect_handler _handler) {
    if (!is_open()) {
        xnet_socket_helper::post_completion(io_context_, std::move(_handler), boost::asio::error::bad_descriptor);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto* its_io = &io_context_;
    if (!transmit_worker_.enqueue([this, its_io, its_socket, its_epoch, endpoint = _ep, handler = std::move(_handler)]() mutable {
        boost::system::error_code its_error;

        nxsockaddr_storage its_storage{};
        nxsocklen_t its_length = 0;
        if (!xnet_socket_helper::endpoint_to_native(endpoint.address(), endpoint.port(), its_storage, its_length, its_error)) {
            xnet_socket_helper::post_completion(*its_io, std::move(handler), its_error);
            return;
        }

        const auto its_result = xnet_api::nxconnect(its_socket, reinterpret_cast<nxsockaddr*>(&its_storage), its_length);

        if (its_result == SOCKET_ERROR_VALUE) {
            its_error = xnet_socket_helper::get_xnet_error();
            if (xnet_socket_helper::is_would_block_like(its_error)) {
                boost::system::error_code its_wait_error;
                if (xnet_socket_helper::wait_socket_ready(its_socket, std::nullopt, boost::asio::ip::tcp::socket::wait_write,
                                                          cancel_epoch_, its_epoch, its_wait_error)) {
                    int32_t its_so_error = 0;
                    nxsocklen_t its_so_error_len = static_cast<nxsocklen_t>(sizeof(its_so_error));
                    if (xnet_api::nxgetsockopt(its_socket, nxSOL_SOCKET, nxSO_ERROR, &its_so_error, &its_so_error_len) == SOCKET_ERROR_VALUE
                        || its_so_error_len < static_cast<nxsocklen_t>(sizeof(its_so_error))) {
                        its_error = xnet_socket_helper::get_xnet_error();
                    } else if (its_so_error != 0) {
                        its_error = xnet_to_boost_error(static_cast<int>(its_so_error));
                    } else {
                        its_error.clear();
                    }
                } else {
                    its_error = its_wait_error;
                }
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
        }

        xnet_socket_helper::post_completion(*its_io, std::move(handler), its_error);
    })) {
        xnet_socket_helper::post_completion(io_context_, std::move(_handler), boost::asio::error::operation_aborted);
    }
}

void xnet_tcp_socket::async_receive(boost::asio::mutable_buffer _b, rw_handler _handler) {
    if (!is_open()) {
        xnet_socket_helper::post_rw_completion(io_context_, std::move(_handler), boost::asio::error::bad_descriptor, 0);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto receive_data = std::make_shared<std::vector<std::uint8_t>>(_b.size());
    const auto its_buffer_size = static_cast<int32_t>(xnet_socket_helper::clamp_size_to_int32(receive_data->size()));

    auto* its_io = &io_context_;
    if (!receive_worker_.enqueue([this, its_io, its_socket, its_epoch, caller_buffer = _b, receive_data, its_buffer_size,
                       handler = std::move(_handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t its_bytes_received = 0;

        for (;;) {
            auto* const its_buffer = receive_data->empty() ? nullptr : receive_data->data();
            const auto its_result = xnet_api::nxrecv(its_socket, its_buffer, its_buffer_size, 0);

            if (its_result > 0) {
                its_bytes_received = static_cast<std::size_t>(its_result);
                its_error.clear();
                break;
            }

            if (its_result == 0) {
                its_error = boost::asio::error::eof;
                break;
            }

            its_error = xnet_socket_helper::get_xnet_error();
            if (its_error == boost::asio::error::not_connected) {
                its_error = boost::asio::error::eof;
            }
            if (!xnet_socket_helper::is_would_block_like(its_error)) {
                break;
            }

            boost::system::error_code its_wait_error;
            if (!xnet_socket_helper::wait_socket_ready(its_socket, std::nullopt, boost::asio::ip::tcp::socket::wait_read,
                                                       cancel_epoch_, its_epoch, its_wait_error)) {
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
        xnet_socket_helper::post_rw_completion(io_context_, std::move(_handler), boost::asio::error::operation_aborted, 0);
    }
}

void xnet_tcp_socket::async_write(std::vector<boost::asio::const_buffer> const& _bs, rw_handler _handler) {
    if (!is_open()) {
        xnet_socket_helper::post_rw_completion(io_context_, std::move(_handler), boost::asio::error::bad_descriptor, 0);
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
                xnet_socket_helper::post_rw_completion(io_context_, std::move(_handler), boost::asio::error::invalid_argument, 0);
                return;
            }
            its_owned_buffer.assign(its_data, its_data + b.size());
        }
        buffers->emplace_back(std::move(its_owned_buffer));
    }

    auto* its_io = &io_context_;
    if (!transmit_worker_.enqueue([this, its_io, its_socket, its_epoch, buffers, handler = std::move(_handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t total_sent = 0;

        for (const auto& b : *buffers) {
            auto* const data_ptr = b.empty() ? nullptr : b.data();
            std::size_t sent_in_buffer = 0;

            while (sent_in_buffer < b.size()) {
                if (is_canceled(its_epoch)) {
                    its_error = boost::asio::error::operation_aborted;
                    total_sent = 0;
                    xnet_socket_helper::post_rw_completion(*its_io, std::move(handler), its_error, total_sent);
                    return;
                }

                const auto remaining = b.size() - sent_in_buffer;
                const auto chunk = static_cast<int32_t>(xnet_socket_helper::clamp_size_to_int32(remaining));

                const auto result = xnet_api::nxsend(its_socket, data_ptr + sent_in_buffer, chunk, 0);
                if (result > 0) {
                    const auto sent = static_cast<std::size_t>(result);
                    sent_in_buffer += sent;
                    total_sent += sent;
                    continue;
                }

                if (result == 0) {
                    its_error = boost::asio::error::eof;
                    xnet_socket_helper::post_rw_completion(*its_io, std::move(handler), its_error, total_sent);
                    return;
                }

                its_error = xnet_socket_helper::get_xnet_error();
                if (!xnet_socket_helper::is_would_block_like(its_error)) {
                    xnet_socket_helper::post_rw_completion(*its_io, std::move(handler), its_error, is_canceled(its_epoch) ? 0 : total_sent);
                    return;
                }

                boost::system::error_code its_wait_error;
                if (!xnet_socket_helper::wait_socket_ready(its_socket, std::nullopt, boost::asio::ip::tcp::socket::wait_write,
                                                           cancel_epoch_, its_epoch, its_wait_error)) {
                    its_error = its_wait_error;
                    xnet_socket_helper::post_rw_completion(*its_io, std::move(handler), its_error, its_error == boost::asio::error::operation_aborted ? 0 : total_sent);
                    return;
                }
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
            total_sent = 0;
        }

        xnet_socket_helper::post_rw_completion(*its_io, std::move(handler), its_error, total_sent);
    })) {
        xnet_socket_helper::post_rw_completion(io_context_, std::move(_handler), boost::asio::error::operation_aborted, 0);
    }
}

void xnet_tcp_socket::async_write(boost::asio::const_buffer const& _b, completion_condition _cc, rw_handler _handler) {
    if (!is_open()) {
        xnet_socket_helper::post_rw_completion(io_context_, std::move(_handler), boost::asio::error::bad_descriptor, 0);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);

    auto data = std::make_shared<std::vector<std::uint8_t>>();
    if (_b.size() > 0) {
        auto* ptr = static_cast<const std::uint8_t*>(_b.data());
        if (ptr == nullptr) {
            xnet_socket_helper::post_rw_completion(io_context_, std::move(_handler), boost::asio::error::invalid_argument, 0);
            return;
        }
        data->assign(ptr, ptr + _b.size());
    }

    auto* its_io = &io_context_;
    if (!transmit_worker_.enqueue([this, its_io, its_socket, its_epoch, data, cc = std::move(_cc), handler = std::move(_handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t total_sent = 0;

        while (total_sent < data->size()) {
            if (is_canceled(its_epoch)) {
                its_error = boost::asio::error::operation_aborted;
                total_sent = 0;
                break;
            }

            const auto its_remaining = data->size() - total_sent;
            const auto its_chunk = static_cast<int32_t>(xnet_socket_helper::clamp_size_to_int32(its_remaining));

            const auto its_result = xnet_api::nxsend(its_socket, data->data() + total_sent, its_chunk, 0);

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

            its_error = xnet_socket_helper::get_xnet_error();
            if (!xnet_socket_helper::is_would_block_like(its_error)) {
                break;
            }

            boost::system::error_code its_wait_error;
            if (!xnet_socket_helper::wait_socket_ready(its_socket, std::nullopt, boost::asio::ip::tcp::socket::wait_write,
                                                       cancel_epoch_, its_epoch, its_wait_error)) {
                its_error = its_wait_error;
                break;
            }
        }

        if (its_error == boost::asio::error::operation_aborted) {
            total_sent = 0;
        }

        xnet_socket_helper::post_rw_completion(*its_io, std::move(handler), its_error, total_sent);
    })) {
        xnet_socket_helper::post_rw_completion(io_context_, std::move(_handler), boost::asio::error::operation_aborted, 0);
    }
}

#if defined(__linux__)
bool xnet_tcp_socket::set_user_timeout(unsigned int _timeout) {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    VSOMEIP_WARNING_P << "unsupported by XNET API, timeout=" << _timeout;
    errno = ENOTSUP;
    return false;
}

bool xnet_tcp_socket::set_keepidle(uint32_t _idle) {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    VSOMEIP_WARNING_P << "unsupported by XNET API, idle=" << _idle;
    errno = ENOTSUP;
    return false;
}

bool xnet_tcp_socket::set_keepintvl(uint32_t _interval) {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    VSOMEIP_WARNING_P << "unsupported by XNET API, interval=" << _interval;
    errno = ENOTSUP;
    return false;
}

bool xnet_tcp_socket::set_keepcnt(uint32_t _count) {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    VSOMEIP_WARNING_P << "unsupported by XNET API, count=" << _count;
    errno = ENOTSUP;
    return false;
}

bool xnet_tcp_socket::set_quick_ack() {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    VSOMEIP_WARNING_P << "unsupported by XNET API";
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

    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_BINDTODEVICE, _device.c_str(), static_cast<nxsocklen_t>(_device.size()))
        == SOCKET_ERROR_VALUE) {
    return false;
}

return true;
}

bool xnet_tcp_socket::can_read_fd_flags() {
    return true;
}
#endif

bool xnet_tcp_socket::is_canceled(std::uint64_t _epoch) const {
    return cancel_epoch_.load(std::memory_order_relaxed) != _epoch;
}

void xnet_tcp_socket::stop_worker_threads() {
    transmit_worker_.request_stop();
    receive_worker_.request_stop();

    transmit_worker_.join();
    receive_worker_.join();
}

}
