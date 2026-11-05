#pragma once

#include <map>
#include <mutex>

#include "abstract_socket_factory.hpp"

namespace vsomeip_v3 {

class xnet_socket_factory final : public abstract_socket_factory {
public:
    ~xnet_socket_factory() override = default;

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

    std::shared_ptr<abstract_clock> get_clock() override;

    void register_ip_stack(boost::asio::io_context& _io, nxIpStackRef_t _stack) override;

    void register_ip_stack(boost::asio::io_context& _io, boost::asio::io_context& _auxiliary_io) override;

    void unregister_ip_stack(boost::asio::io_context& _io) override;

    bool is_xnet_backend() const override { return true; }

private:
    nxIpStackRef_t get_xnet_ip_stack(boost::asio::io_context& _io) const;

    mutable std::mutex stacks_mutex_;
    std::map<const boost::asio::io_context*, nxIpStackRef_t> stacks_;
};

}