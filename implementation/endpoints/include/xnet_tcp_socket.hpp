// XNET TCP Socket Wrapper for vsomeip

#ifndef XNET_TCP_SOCKET_HPP_
#define XNET_TCP_SOCKET_HPP_

#include "tcp_socket.hpp"
#include "xnet_socket_helper.hpp"

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <memory>
#include <string>

#include "nxsocket.h"

namespace vsomeip_v3 {

class xnet_tcp_acceptor;

class xnet_tcp_socket final : public tcp_socket {
public:
    xnet_tcp_socket(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack);
    ~xnet_tcp_socket() override;

private:
    friend class xnet_tcp_acceptor;

    void assign_accepted_socket(nxSOCKET _socket, bool _is_ipv6, boost::system::error_code& _ec);

    [[nodiscard]] bool is_open() const override;
    [[nodiscard]] int native_handle() override;

    void open(boost::asio::ip::tcp::endpoint::protocol_type _pt, boost::system::error_code& _ec) override;
    void bind(boost::asio::ip::tcp::endpoint const& _ep, boost::system::error_code& _ec) override;

    void close(boost::system::error_code& _ec) override;
    void cancel(boost::system::error_code& _ec) override;

    boost::asio::ip::tcp::endpoint local_endpoint(boost::system::error_code& _ec) const override;

    void io_control(io_control_operation<std::size_t>& _icm, boost::system::error_code& _ec) override;

    void set_option(boost::asio::ip::tcp::no_delay _nd, boost::system::error_code& _ec) override;
    void set_option(boost::asio::ip::tcp::socket::keep_alive _ka, boost::system::error_code& _ec) override;
    void set_option(boost::asio::ip::tcp::socket::linger _l, boost::system::error_code& _ec) override;
    void set_option(boost::asio::ip::tcp::socket::reuse_address _ra, boost::system::error_code& _ec) override;

    void async_connect(boost::asio::ip::tcp::endpoint const& _ep, connect_handler _handler) override;
    void async_receive(boost::asio::mutable_buffer _b, rw_handler _handler) override;
    void async_write(std::vector<boost::asio::const_buffer> const& _bs, rw_handler _handler) override;
    void async_write(boost::asio::const_buffer const& _b, completion_condition _cc, rw_handler _handler) override;

#if defined(__linux__)
    [[nodiscard]] bool set_user_timeout(unsigned int _timeout) override;
    [[nodiscard]] bool set_keepidle(uint32_t _idle) override;
    [[nodiscard]] bool set_keepintvl(uint32_t _interval) override;
    [[nodiscard]] bool set_keepcnt(uint32_t _count) override;
    [[nodiscard]] bool set_quick_ack() override;
#endif

#if defined(__linux__) || defined(__QNX__)
    [[nodiscard]] bool bind_to_device(std::string const& _device) override;
    [[nodiscard]] bool can_read_fd_flags() override;
#endif

    [[nodiscard]] bool is_canceled(std::uint64_t _epoch) const;

    void stop_worker_threads();

    // XNET socket handle
    nxSOCKET socket_;

    // Reference to io_context for posting completion handlers
    boost::asio::io_context& io_context_;

    // XNET IP stack reference
    nxIpStackRef_t xnet_stack_;

    // Track if socket is IPv6 for correct socket creation and option handling
    bool is_ipv6_;

    // Epoch counter for cancellation
    std::atomic<std::uint64_t> cancel_epoch_;

    // Worker for blocking send operations
    xnet_socket_helper::worker_thread transmit_worker_;

    // Worker for blocking receive operations
    xnet_socket_helper::worker_thread receive_worker_;
};

}

#endif
