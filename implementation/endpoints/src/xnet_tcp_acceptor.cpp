#include <algorithm>
#include <cstring>
#include <limits>

#include "../include/xnet_socket_helper.hpp"
#include "../include/xnet_tcp_acceptor.hpp"
#include "../include/xnet_tcp_socket.hpp"
#include "../include/xnet_error.hpp"
#include "../include/xnet_api.hpp"

#include "logger_ext.hpp"

#define VSOMEIP_LOG_PREFIX "[XNET][tcp-acceptor]"

#define SOCKET_ERROR_VALUE -1

namespace vsomeip_v3 {

xnet_tcp_acceptor::xnet_tcp_acceptor(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack)
    : acceptor_(nxINVALID_SOCKET),
      io_context_(_io),
      xnet_stack_(_xnet_stack),
      is_ipv6_(false),
      cancel_epoch_(0) {
}

xnet_tcp_acceptor::~xnet_tcp_acceptor() {
    boost::system::error_code ec;
    close(ec);
}

bool xnet_tcp_acceptor::is_open() const {
    return acceptor_ != nxINVALID_SOCKET;
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

    acceptor_ = xnet_api::nxsocket(xnet_stack_, is_ipv6_ ? nxAF_INET6 : nxAF_INET, nxSOCK_STREAM, nxIPPROTO_TCP);

    if (acceptor_ == nxINVALID_SOCKET) {
        _ec = xnet_socket_helper::get_xnet_error();
        return;
    }

    _ec.clear();
}

bool xnet_tcp_acceptor::wait_for_pending_connection(std::chrono::milliseconds _timeout, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return false;
    }

    return xnet_socket_helper::wait_socket_ready(acceptor_, _timeout, boost::asio::socket_base::wait_read, cancel_epoch_,
                                                 cancel_epoch_.load(std::memory_order_relaxed), _ec);
}

void xnet_tcp_acceptor::bind(boost::asio::ip::tcp::endpoint const& _ep, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    nxsockaddr_storage its_storage{};
    nxsocklen_t its_len = 0;
    if (!xnet_socket_helper::endpoint_to_native(_ep.address(), _ep.port(), its_storage, its_len, _ec)) {
        return;
    }

    if (xnet_api::nxbind(acceptor_, reinterpret_cast<nxsockaddr*>(&its_storage), its_len) == SOCKET_ERROR_VALUE) {
        _ec = xnet_socket_helper::get_xnet_error();
        return;
    }

    _ec.clear();
}

void xnet_tcp_acceptor::close(boost::system::error_code& _ec) {
    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);

    if (is_open()) {
        if (xnet_api::nxclose(acceptor_) == SOCKET_ERROR_VALUE) {
            _ec = xnet_socket_helper::get_xnet_error();
            acceptor_ = nxINVALID_SOCKET;
            stop_worker_thread();
            return;
        }
        acceptor_ = nxINVALID_SOCKET;
    }
    _ec.clear();
    stop_worker_thread();
}

void xnet_tcp_acceptor::cancel(boost::system::error_code& _ec) {
    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);
    _ec.clear();
}

void xnet_tcp_acceptor::listen(int _backlog, boost::system::error_code& _ec) {
    if (!is_open()) {
        _ec = boost::asio::error::bad_descriptor;
        return;
    }

    const auto its_backlog = std::min(_backlog, static_cast<int>(std::numeric_limits<int16_t>::max()));
    if (xnet_api::nxlisten(acceptor_, its_backlog) == SOCKET_ERROR_VALUE) {
        _ec = xnet_socket_helper::get_xnet_error();
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
    if (xnet_api::nxsetsockopt(acceptor_, nxSOL_SOCKET, nxSO_REUSEADDR, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        _ec = xnet_socket_helper::get_xnet_error();
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

    (void)xnet_socket_helper::make_unsupported_option_warning(VSOMEIP_LOG_PREFIX, "set_reuse_port", "SO_REUSEPORT is not supported by XNET API");
    errno = ENOTSUP;
    return false;
}

bool xnet_tcp_acceptor::set_native_option_free_bind() {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    (void)xnet_socket_helper::make_unsupported_option_warning(VSOMEIP_LOG_PREFIX, "set_native_option_free_bind", "IP_FREEBIND is not supported by XNET API");
    errno = ENOTSUP;
    return false;
}
#endif

#if defined(__linux__) || defined(__QNX__)
bool xnet_tcp_acceptor::bind_to_device(std::string const& _device) {
    if (!is_open()) {
        errno = EBADF;
        return false;
    }

    if (xnet_api::nxsetsockopt(acceptor_, nxSOL_SOCKET, nxSO_BINDTODEVICE, _device.c_str(), static_cast<nxsocklen_t>(_device.size()))
        == SOCKET_ERROR_VALUE) {
        return false;
    }
    return true;
}
#endif

void xnet_tcp_acceptor::async_accept(tcp_socket& _socket, boost::asio::ip::tcp::endpoint& _peer_ep, connect_handler _handler) {
    if (!is_open()) {
        xnet_socket_helper::post_completion(io_context_, std::move(_handler), boost::asio::error::bad_descriptor);
        return;
    }

    auto* its_socket = dynamic_cast<xnet_tcp_socket*>(&_socket);
    if (!its_socket) {
        xnet_socket_helper::post_completion(io_context_, std::move(_handler), boost::asio::error::invalid_argument);
        return;
    }

    const auto its_acceptor = acceptor_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto* const its_io = &io_context_;

    if (!worker_.enqueue([this, its_acceptor, its_epoch, its_socket, peer_ep = std::ref(_peer_ep), handler = std::move(_handler), its_io]() mutable {
        boost::system::error_code its_error;
        nxsockaddr_storage its_peer_storage{};
        nxsocklen_t its_peer_len = static_cast<nxsocklen_t>(sizeof(its_peer_storage));
        nxSOCKET its_client_socket = nxINVALID_SOCKET;

        for (;;) {
            if (is_canceled(its_epoch)) {
                its_error = boost::asio::error::operation_aborted;
                break;
            }

            boost::system::error_code its_wait_error;
            if (!xnet_socket_helper::wait_socket_ready(its_acceptor, std::nullopt, boost::asio::socket_base::wait_read,
                                                       cancel_epoch_, its_epoch, its_wait_error)) {
                its_error = its_wait_error;
                break;
            }

            its_client_socket = xnet_api::nxaccept(its_acceptor, reinterpret_cast<nxsockaddr*>(&its_peer_storage), &its_peer_len);

            if (its_client_socket == nxINVALID_SOCKET) {
                its_error = xnet_socket_helper::get_xnet_error();
                if (xnet_socket_helper::is_would_block_like(its_error) || its_error == boost::asio::error::interrupted) {
                    continue;
                }
                break;
            }

            boost::asio::ip::address endpoint_address;
            unsigned short endpoint_port;
            if (!xnet_socket_helper::native_to_endpoint(its_peer_storage, its_peer_len, endpoint_address, endpoint_port, its_error)) {
                (void)xnet_api::nxclose(its_client_socket);
                its_client_socket = nxINVALID_SOCKET;
                break;
            }
            peer_ep.get() = boost::asio::ip::tcp::endpoint(endpoint_address, endpoint_port);

            its_socket->assign_accepted_socket(its_client_socket, peer_ep.get().protocol() == boost::asio::ip::tcp::v6(), its_error);
            if (its_error) {
                (void)xnet_api::nxclose(its_client_socket);
                its_client_socket = nxINVALID_SOCKET;
            }
            break;
        }

        if (is_canceled(its_epoch) && its_error != boost::asio::error::operation_aborted) {
            its_error = boost::asio::error::operation_aborted;
        }

        xnet_socket_helper::post_completion(*its_io, std::move(handler), its_error);
    })) {
        xnet_socket_helper::post_completion(io_context_, std::move(_handler), boost::asio::error::operation_aborted);
    }
}

bool xnet_tcp_acceptor::is_canceled(std::uint64_t _epoch) const {
    return cancel_epoch_.load(std::memory_order_relaxed) != _epoch;
}

void xnet_tcp_acceptor::stop_worker_thread() {
    worker_.request_stop();
    worker_.join();
}

} // namespace vsomeip_v3

