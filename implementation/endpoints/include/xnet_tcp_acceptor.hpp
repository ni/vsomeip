// XNET TCP Acceptor Wrapper for vsomeip

#ifndef XNET_TCP_ACCEPTOR_HPP_
#define XNET_TCP_ACCEPTOR_HPP_

#include "tcp_socket.hpp"
#include "xnet_socket_helper.hpp"

#include <atomic>
#include <chrono>
#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <memory>
#include <string>

#include "nxsocket.h"

namespace vsomeip_v3 {

class xnet_tcp_acceptor final : public tcp_acceptor {
public:
    xnet_tcp_acceptor(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack);
    ~xnet_tcp_acceptor() override;

private:
    [[nodiscard]] bool is_open() const override;
    [[nodiscard]] int native_handle() override;

    void open(boost::asio::ip::tcp::endpoint::protocol_type _pt, boost::system::error_code& _ec) override;
    void bind(boost::asio::ip::tcp::endpoint const& _ep, boost::system::error_code& _ec) override;
    void close(boost::system::error_code& _ec) override;
    void cancel(boost::system::error_code& _ec) override;
    void listen(int _backlog, boost::system::error_code& _ec) override;
    [[nodiscard]] bool wait_for_pending_connection(std::chrono::milliseconds _timeout, boost::system::error_code& _ec) override;

    void set_option(boost::asio::ip::tcp::socket::reuse_address _ra, boost::system::error_code& _ec) override;

#if defined(__linux__)
    [[nodiscard]] bool set_reuse_port() override;
    [[nodiscard]] bool set_native_option_free_bind() override;
#endif

#if defined(__linux__) || defined(__QNX__)
    [[nodiscard]] bool bind_to_device(std::string const& _device) override;
#endif

    void async_accept(tcp_socket& _socket, boost::asio::ip::tcp::endpoint& _peer_ep, connect_handler _handler) override;

    [[nodiscard]] bool is_canceled(std::uint64_t _epoch) const;

    void stop_worker_thread();

    // XNET socket handle
    nxSOCKET acceptor_;

    // Reference to io_context for posting completion handlers
    boost::asio::io_context& io_context_;

    // XNET IP stack reference
    nxIpStackRef_t xnet_stack_;

    // Track if socket is IPv6 for correct socket creation and option handling
    bool is_ipv6_;

    // Epoch counter for cancellation
    std::atomic<std::uint64_t> cancel_epoch_;

    // Worker for blocking accept operation
    xnet_socket_helper::worker_thread worker_;
};

} // namespace vsomeip_v3

#endif
