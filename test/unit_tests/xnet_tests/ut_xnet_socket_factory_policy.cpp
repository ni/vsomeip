#include <gtest/gtest.h>

#include "asio_tcp_socket.hpp"
#include "asio_udp_socket.hpp"
#include "asio_timer.hpp"
#include "xnet_socket_factory.hpp"
#include "xnet_tcp_acceptor.hpp"
#include "xnet_tcp_socket.hpp"
#include "xnet_udp_socket.hpp"

#if defined(__linux__) || defined(__QNX__)
#include "asio_uds_acceptor.hpp"
#include asio_uds_socket.hpp"
#endif

TEST(xnet_socket_factory_policy_test, create_timer_uses_asio_timer_in_all_modes) {
    boost::asio::io_context io;
    vsomeip_v3::xnet_socket_factory factory;

    auto timer = factory.create_timer(io);
    EXPECT_NE(dynamic_cast<vsomeip_v3::asio_timer*>(timer.get()), nullptr);
}

#if defined(__linux__) || defined(__QNX__)
TEST(xnet_socket_factory_policy_test, create_uds_objects_use_asio_in_all_modes) {
    boost::asio::io_context io;
    vsomeip_v3::xnet_socket_factory factory;

    auto uds_socket = factory.create_uds_socket(io);
    auto uds_acceptor = factory.create_uds_acceptor(io);

    EXPECT_NE(dynamic_cast<vsomeip_v3::asio_uds_socket*>(uds_socket.get()), nullptr);
    EXPECT_NE(dynamic_cast<vsomeip_v3::asio_uds_acceptor*>(uds_acceptor.get()), nullptr);
}
#endif

TEST(xnet_socket_factory_policy_test, create_transport_objects_throw_when_stack_missing) {
    boost::asio::io_context io;
    vsomeip_v3::xnet_socket_factory factory;

    EXPECT_THROW((void)factory.create_tcp_socket(io), std::runtime_error);
    EXPECT_THROW((void)factory.create_udp_socket(io), std::runtime_error);
    EXPECT_THROW((void)factory.create_tcp_acceptor(io), std::runtime_error);
}

TEST(xnet_socket_factory_policy_test, create_transport_objects_use_xnet_when_stack_present) {
    boost::asio::io_context io;
    auto const fake_stack = reinterpret_cast<nxIpStackRef_t>(0x1);
    vsomeip_v3::xnet_socket_factory factory(fake_stack);

    auto tcp_socket = factory.create_tcp_socket(io);
    auto udp_socket = factory.create_udp_socket(io);
    auto acceptor = factory.create_tcp_acceptor(io);

    EXPECT_NE(dynamic_cast<vsomeip_v3::xnet_tcp_socket*>(tcp_socket.get()), nullptr);
    EXPECT_NE(dynamic_cast<vsomeip_v3::xnet_udp_socket*>(udp_socket.get()), nullptr);
    EXPECT_NE(dynamic_cast<vsomeip_v3::xnet_tcp_acceptor*>(acceptor.get()), nullptr);
}
