#ifndef XNET_SOCKET_FACTORY_HPP_
#define XNET_SOCKET_FACTORY_HPP_

#include <iostream>

#include "abstract_socket_factory.hpp"

#include "xnet_types.hpp"


namespace vsomeip_v3 {

class xnet_socket_factory final : public abstract_socket_factory {
public:
xnet_socket_factory();
explicit xnet_socket_factory(nxIpStackRef_t xnet_stack);
~xnet_socket_factory() override = default;

// Get XNET stack reference
nxIpStackRef_t get_xnet_stack() const { return xnet_stack_; }
bool is_xnet_enabled() const { return xnet_stack_ != nullptr; }

#if defined(__linux__)
    std::shared_ptr<abstract_netlink_connector> create_netlink_connector(boost::asio::io_context& _io,
        const boost::asio::ip::address& _address,
        const boost::asio::ip::address& _multicast_address,
        bool _is_requiring_link) override;
#endif

    std::unique_ptr<tcp_socket> create_tcp_socket(boost::asio::io_context& _io) override;
    std::unique_ptr<tcp_acceptor> create_tcp_acceptor(boost::asio::io_context& _io) override;
    std::unique_ptr<udp_socket> create_udp_socket(boost::asio::io_context& _io) override;

#if defined(__linux__) || defined(__QNX__)
    std::unique_ptr<uds_socket> create_uds_socket(boost::asio::io_context& _io) override;
    std::unique_ptr<uds_acceptor> create_uds_acceptor(boost::asio::io_context& _io) override;
#endif

    std::unique_ptr<abstract_timer> create_timer(boost::asio::io_context& _io) override;

    bool is_xnet_backend() const override;

private:
    nxIpStackRef_t xnet_stack_ = nullptr; // XNET IP stack reference (always null on non-XNET builds)
};

}

#endif
