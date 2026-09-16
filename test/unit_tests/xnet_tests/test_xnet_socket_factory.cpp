#include <gtest/gtest.h>

#include "asio_udp_socket.hpp"
#include "asio_tcp_socket.hpp"
#include "xnet_socket_factory.hpp"
#include "xnet_tcp_acceptor.hpp"
#include "xnet_tcp_socket.hpp"
#include "xnet_udp_socket.hpp"


TEST(test_xnet_socket_factory, create_tcp_acceptor_strategy_is_explicit) {
    boost::asio::io_context io;
    vsomeip_v3::xnet_socket_factory factory;

    EXPECT_THROW((void)factory.create_tcp_acceptor(io), std::runtime_error);
}

TEST(test_xnet_socket_factory, create_tcp_acceptor_uses_xnet_when_stack_is_present) {
    boost::asio::io_context io;
    auto const fake_stack = reinterpret_cast<nxIpStackRef_t>(0x1);
    vsomeip_v3::xnet_socket_factory factory(fake_stack);

    auto acceptor = factory.create_tcp_acceptor(io);

    EXPECT_NE(dynamic_cast<vsomeip_v3::xnet_tcp_acceptor*>(acceptor.get()), nullptr);
}

TEST(test_xnet_socket_factory, create_transport_sockets_use_xnet_when_stack_is_present) {
    boost::asio::io_context io;
    auto const fake_stack = reinterpret_cast<nxIpStackRef_t>(0x1);
    vsomeip_v3::xnet_socket_factory factory(fake_stack);

    auto tcp_socket = factory.create_tcp_socket(io);
    auto udp_socket = factory.create_udp_socket(io);

    EXPECT_NE(dynamic_cast<vsomeip_v3::xnet_tcp_socket*>(tcp_socket.get()), nullptr);
    EXPECT_NE(dynamic_cast<vsomeip_v3::xnet_udp_socket*>(udp_socket.get()), nullptr);
}

TEST(test_xnet_socket_factory, create_transport_sockets_use_asio_when_stack_is_missing) {
    boost::asio::io_context io;
    vsomeip_v3::xnet_socket_factory factory;

    EXPECT_THROW((void)factory.create_tcp_socket(io), std::runtime_error);
    EXPECT_THROW((void)factory.create_udp_socket(io), std::runtime_error);
}
