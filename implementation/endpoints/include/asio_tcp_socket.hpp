// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "backend_socket_option_helpers.hpp"
#include "tcp_socket.hpp"

#include <boost/asio/write.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <chrono>

#if defined(_WIN32)
#include <Winsock2.h>
#else
#include <poll.h>
#include <cerrno>
#endif

#include <limits>
#include <memory>
#include <string>

namespace vsomeip_v3 {

class asio_tcp_acceptor;

class asio_tcp_socket final : public tcp_socket {
public:
    asio_tcp_socket(boost::asio::io_context& _io) : socket_(std::make_shared<boost::asio::ip::tcp::socket>(_io)) { }

private:
    [[nodiscard]] bool is_open() const override { return socket_->is_open(); }
    [[nodiscard]] int native_handle() override { return socket_->native_handle(); }

    void open(boost::asio::ip::tcp::endpoint::protocol_type pt, boost::system::error_code& ec) override { socket_->open(pt, ec); }
    void bind(boost::asio::ip::tcp::endpoint const& ep, boost::system::error_code& ec) override { socket_->bind(ep, ec); }

    void close(boost::system::error_code& ec) override { socket_->close(ec); }
    void cancel(boost::system::error_code& ec) override { socket_->cancel(ec); }
    void io_control(io_control_operation<std::size_t>& icm, boost::system::error_code& ec) override { socket_->io_control(icm, ec); }
    void set_option(boost::asio::ip::tcp::no_delay nd, boost::system::error_code& ec) override { socket_->set_option(nd, ec); }
    void set_option(boost::asio::ip::tcp::socket::keep_alive ka, boost::system::error_code& ec) override { socket_->set_option(ka, ec); }
    void set_option(boost::asio::ip::tcp::socket::linger l, boost::system::error_code& ec) override { socket_->set_option(l, ec); }
    void set_option(boost::asio::ip::tcp::socket::reuse_address ra, boost::system::error_code& ec) override { socket_->set_option(ra, ec); }
#if defined(__linux__)
    [[nodiscard]] bool set_user_timeout(unsigned int timeout) override {
        return socket_option_helpers::set_tcp_user_timeout(socket_->native_handle(), timeout);
    }
    [[nodiscard]] bool set_keepidle(uint32_t idle) override {
        return socket_option_helpers::set_tcp_keepidle(socket_->native_handle(), idle);
    }
    [[nodiscard]] bool set_keepintvl(uint32_t interval) override {
        return socket_option_helpers::set_tcp_keepintvl(socket_->native_handle(), interval);
    }
    [[nodiscard]] bool set_keepcnt(uint32_t count) override {
        return socket_option_helpers::set_tcp_keepcnt(socket_->native_handle(), count);
    }
    [[nodiscard]] bool set_quick_ack() override {
        return socket_option_helpers::set_tcp_quick_ack(socket_->native_handle());
    }
#endif
#if defined(__linux__) || defined(__QNX__)
    [[nodiscard]] bool bind_to_device(std::string const& _device) override {
        return socket_option_helpers::set_bind_to_device(socket_->native_handle(), _device);
    }
    [[nodiscard]] bool can_read_fd_flags() override { return fcntl(socket_->native_handle(), F_GETFD) != -1; }
#endif
    boost::asio::ip::tcp::endpoint local_endpoint(boost::system::error_code& ec) const override { return socket_->local_endpoint(ec); }
    void async_connect(boost::asio::ip::tcp::endpoint const& ep, connect_handler handler) override {
        auto socket = socket_;
        socket->async_connect(ep, [f = std::move(handler), socket](auto const& _ec) { f(_ec); });
    }
    void async_receive(boost::asio::mutable_buffer b, rw_handler handler) override {
        auto socket = socket_;
        socket->async_receive(b, [f = std::move(handler), socket](auto const& _ec, size_t _bytes) { f(_ec, _bytes); });
    }
    void async_write(std::vector<boost::asio::const_buffer> const& bs, rw_handler handler) override {
        auto socket = socket_;
        boost::asio::async_write(*socket, bs, [f = std::move(handler), socket](auto const& _ec, size_t _bytes) { f(_ec, _bytes); });
    }
    void async_write(boost::asio::const_buffer const& b, completion_condition cc, rw_handler handler) override {
        auto socket = socket_;
        boost::asio::async_write(*socket, b, std::move(cc),
                                 [f = std::move(handler), socket](auto const& _ec, size_t _bytes) { f(_ec, _bytes); });
    }

    // needs to access the socket member to create a meaningful new connection
    friend class asio_tcp_acceptor;
    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
};

class asio_tcp_acceptor final : public tcp_acceptor {
public:
    asio_tcp_acceptor(boost::asio::io_context& _io) : acceptor_(std::make_shared<boost::asio::ip::tcp::acceptor>(_io)) { }

private:
    [[nodiscard]] bool is_open() const override { return acceptor_->is_open(); }
    [[nodiscard]] int native_handle() override { return acceptor_->native_handle(); }

    void open(boost::asio::ip::tcp::endpoint::protocol_type pt, boost::system::error_code& ec) override { acceptor_->open(pt, ec); }
    void bind(boost::asio::ip::tcp::endpoint const& ep, boost::system::error_code& ec) override { acceptor_->bind(ep, ec); }
    void close(boost::system::error_code& ec) override { acceptor_->close(ec); }
    void cancel(boost::system::error_code& ec) override { acceptor_->cancel(ec); }
    void listen(int backlog, boost::system::error_code& ec) override { acceptor_->listen(backlog, ec); }

    bool wait_for_pending_connection(std::chrono::milliseconds timeout, boost::system::error_code& ec) override {
        if (!acceptor_->is_open()) {
            ec = boost::asio::error::bad_descriptor;
            return false;
        }

        if (timeout.count() < 0 || timeout.count() > std::numeric_limits<int>::max()) {
            ec = boost::asio::error::invalid_argument;
            return false;
        }

        const int timeout_ms = static_cast<int>(timeout.count());

#if defined(_WIN32)
        WSAPOLLFD pfd{};
        pfd.fd = acceptor_->native_handle();
        pfd.events = POLLIN;

        const int rc = ::WSAPoll(&pfd, 1, timeout_ms);
        if (rc < 0) {
            ec = boost::system::error_code(WSAGetLastError(), boost::asio::error::get_system_category());
            return false;
        }
        if (rc == 0) {
            ec.clear();
            return false;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            ec = boost::asio::error::fault;
            return false;
        }

        ec.clear();
        return (pfd.revents & POLLIN) != 0;
#else
        pollfd pfd{};
        pfd.fd = acceptor_->native_handle();
        pfd.events = POLLIN;

        const int rc = ::poll(&pfd, 1, timeout_ms);
        if (rc < 0) {
            ec.assign(errno, boost::system::system_category());
            return false;
        }
        if (rc == 0) {
            ec.clear();
            return false;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            ec = boost::asio::error::fault;
            return false;
        }

        ec.clear();
        return (pfd.revents & POLLIN) != 0;
#endif
    }


    void set_option(boost::asio::ip::tcp::socket::reuse_address ra, boost::system::error_code& ec) override {
        acceptor_->set_option(ra, ec);
    }

#if defined(__linux__)
    [[nodiscard]] bool set_reuse_port() override {
        return socket_option_helpers::set_tcp_acceptor_reuse_port(acceptor_->native_handle());
            setsockopt(acceptor_->native_handle(), SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag)) != -1;
    }

    [[nodiscard]] bool set_native_option_free_bind() override {
        return socket_option_helpers::set_tcp_acceptor_free_bind(acceptor_->native_handle());
    }
#endif
#if defined(__linux__) || defined(__QNX__)
    [[nodiscard]] bool bind_to_device(std::string const& _device) override {
        return socket_option_helpers::set_bind_to_device(socket_->native_handle(), _device);
    }
#endif
    void async_accept(tcp_socket& _socket, boost::asio::ip::tcp::endpoint& _peer_ep, connect_handler _handler) override {
        auto* socket_impl = dynamic_cast<asio_tcp_socket*>(&_socket);
        if (!socket_impl || !socket_impl->socket_) {
            _handler(boost::asio::error::make_error_code(boost::asio::error::invalid_argument));
            return;
        }
        auto acceptor = acceptor_;
        auto socket = socket_impl->socket_;
        acceptor->async_accept(*socket, _peer_ep, [f = std::move(_handler), acceptor, socket](auto const& _ec) { f(_ec); });
    }

    std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
};

}
