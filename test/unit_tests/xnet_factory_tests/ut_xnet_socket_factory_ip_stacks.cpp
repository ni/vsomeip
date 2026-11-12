#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "asio_socket_factory.hpp"
#include "xnet_socket_factory.hpp"
#include "xnet_tcp_acceptor.hpp"
#include "xnet_tcp_socket.hpp"
#include "xnet_udp_socket.hpp"

namespace {

nxIpStackRef_t fake_stack(std::uintptr_t _value) {
    return reinterpret_cast<nxIpStackRef_t>(_value);
}

}

TEST(xnet_socket_factory_ip_stacks_test, is_xnet_backend) {
    vsomeip_v3::xnet_socket_factory factory;
    EXPECT_TRUE(factory.is_xnet_backend());
}

TEST(xnet_socket_factory_ip_stacks_test, asio_factory_is_not_xnet_backend_and_ignores_stacks) {
    boost::asio::io_context io;
    vsomeip_v3::asio_socket_factory factory;

    EXPECT_FALSE(factory.is_xnet_backend());
    factory.register_ip_stack(io, fake_stack(0x1));
    EXPECT_EQ(factory.get_xnet_ip_stack(io), nullptr);
}

TEST(xnet_socket_factory_ip_stacks_test, multiple_io_contexts_keep_separate_stacks) {
    boost::asio::io_context io_a;
    boost::asio::io_context io_b;
    vsomeip_v3::xnet_socket_factory factory;

    factory.register_ip_stack(io_a, fake_stack(0x10));
    factory.register_ip_stack(io_b, fake_stack(0x20));

    EXPECT_EQ(factory.get_xnet_ip_stack(io_a), fake_stack(0x10));
    EXPECT_EQ(factory.get_xnet_ip_stack(io_b), fake_stack(0x20));
}

TEST(xnet_socket_factory_ip_stacks_test, re_registering_overwrites_stack) {
    boost::asio::io_context io;
    vsomeip_v3::xnet_socket_factory factory;

    factory.register_ip_stack(io, fake_stack(0x10));
    factory.register_ip_stack(io, fake_stack(0x30));

    EXPECT_EQ(factory.get_xnet_ip_stack(io), fake_stack(0x30));
}

TEST(xnet_socket_factory_ip_stacks_test, unregister_removes_only_given_io_context) {
    boost::asio::io_context io_a;
    boost::asio::io_context io_b;
    vsomeip_v3::xnet_socket_factory factory;

    factory.register_ip_stack(io_a, fake_stack(0x10));
    factory.register_ip_stack(io_b, fake_stack(0x20));

    factory.unregister_ip_stack(io_a);

    EXPECT_EQ(factory.get_xnet_ip_stack(io_a), nullptr);
    EXPECT_EQ(factory.get_xnet_ip_stack(io_b), fake_stack(0x20));
}

TEST(xnet_socket_factory_ip_stacks_test, unregister_unknown_io_context_is_noop) {
    boost::asio::io_context io;
    vsomeip_v3::xnet_socket_factory factory;

    EXPECT_NO_THROW(factory.unregister_ip_stack(io));
    EXPECT_EQ(factory.get_xnet_ip_stack(io), nullptr);
}

TEST(xnet_socket_factory_ip_stacks_test, auxiliary_io_context_inherits_stack) {
    boost::asio::io_context io;
    boost::asio::io_context auxiliary_io;
    vsomeip_v3::xnet_socket_factory factory;

    factory.register_ip_stack(io, fake_stack(0x10));
    factory.register_ip_stack(io, auxiliary_io);

    EXPECT_EQ(factory.get_xnet_ip_stack(io), fake_stack(0x10));
    EXPECT_EQ(factory.get_xnet_ip_stack(auxiliary_io), fake_stack(0x10));
}

TEST(xnet_socket_factory_ip_stacks_test, auxiliary_io_context_without_primary_stack_is_not_registered) {
    boost::asio::io_context io;
    boost::asio::io_context auxiliary_io;
    vsomeip_v3::xnet_socket_factory factory;

    factory.register_ip_stack(io, auxiliary_io);

    EXPECT_EQ(factory.get_xnet_ip_stack(io), nullptr);
    EXPECT_EQ(factory.get_xnet_ip_stack(auxiliary_io), nullptr);
}

TEST(xnet_socket_factory_ip_stacks_test, auxiliary_io_context_survives_primary_unregister) {
    boost::asio::io_context io;
    boost::asio::io_context auxiliary_io;
    vsomeip_v3::xnet_socket_factory factory;

    factory.register_ip_stack(io, fake_stack(0x10));
    factory.register_ip_stack(io, auxiliary_io);
    factory.unregister_ip_stack(io);

    EXPECT_EQ(factory.get_xnet_ip_stack(io), nullptr);
    EXPECT_EQ(factory.get_xnet_ip_stack(auxiliary_io), fake_stack(0x10));

    factory.unregister_ip_stack(auxiliary_io);
    EXPECT_EQ(factory.get_xnet_ip_stack(auxiliary_io), nullptr);
}

TEST(xnet_socket_factory_ip_stacks_test, concurrent_registration_is_thread_safe) {
    constexpr std::size_t thread_count = 8;
    vsomeip_v3::xnet_socket_factory factory;
    std::vector<std::unique_ptr<boost::asio::io_context>> contexts;
    for (std::size_t i = 0; i < thread_count; ++i) {
        contexts.emplace_back(std::make_unique<boost::asio::io_context>());
    }

    std::atomic<std::size_t> mismatches{0};
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < thread_count; ++i) {
        threads.emplace_back([&, i]() {
            auto& io = *contexts[i];
            auto const stack = fake_stack(0x100 + i);
            for (int n = 0; n < 1000; ++n) {
                factory.register_ip_stack(io, stack);
                if (factory.get_xnet_ip_stack(io) != stack) {
                    ++mismatches;
                }
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(mismatches.load(), 0U);
    for (std::size_t i = 0; i < thread_count; ++i) {
        EXPECT_EQ(factory.get_xnet_ip_stack(*contexts[i]), fake_stack(0x100 + i));
    }
}
