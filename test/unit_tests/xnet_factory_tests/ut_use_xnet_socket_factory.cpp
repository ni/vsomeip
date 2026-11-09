// Tests for use_xnet_socket_factory().
//
// The global socket factory is fixed by the first abstract_socket_factory::get()
// call, so every test calls use_xnet_socket_factory() first. The late-injection
// scenario (factory already initialized as ASIO) runs in a child process through
// a "threadsafe" death test, which re-executes this binary for that test only.

#include <cstdint>
#include <cstdlib>

#include <gtest/gtest.h>

#include <vsomeip/xnet.hpp>

#include "abstract_socket_factory.hpp"
#include "asio_socket_factory.hpp"
#include "xnet_socket_factory.hpp"

namespace {

nxIpStackRef_t fake_stack(std::uintptr_t _value) {
    return reinterpret_cast<nxIpStackRef_t>(_value);
}

}

TEST(use_xnet_socket_factory_test, installs_xnet_socket_factory) {
    EXPECT_TRUE(vsomeip_v3::use_xnet_socket_factory());

    auto factory = vsomeip_v3::abstract_socket_factory::get();
    ASSERT_NE(factory, nullptr);
    EXPECT_TRUE(factory->is_xnet_backend());
    EXPECT_NE(dynamic_cast<vsomeip_v3::xnet_socket_factory*>(factory), nullptr);
}

TEST(use_xnet_socket_factory_test, repeated_calls_keep_same_factory) {
    EXPECT_TRUE(vsomeip_v3::use_xnet_socket_factory());
    auto first = vsomeip_v3::abstract_socket_factory::get();

    EXPECT_TRUE(vsomeip_v3::use_xnet_socket_factory());

    EXPECT_EQ(vsomeip_v3::abstract_socket_factory::get(), first);
    EXPECT_TRUE(vsomeip_v3::abstract_socket_factory::get()->is_xnet_backend());
}

TEST(use_xnet_socket_factory_test, late_call_returns_false_and_keeps_asio_factory) {
    ::testing::GTEST_FLAG(death_test_style) = "threadsafe";

    EXPECT_EXIT(
            {
                // Runs in a fresh child process: initialize the default (ASIO) factory first.
                auto initial = vsomeip_v3::abstract_socket_factory::get();
                bool const is_xnet_active = vsomeip_v3::use_xnet_socket_factory();

                auto factory = vsomeip_v3::abstract_socket_factory::get();
                boost::asio::io_context io;
                factory->register_ip_stack(io, fake_stack(0x1));

                bool const ok = !is_xnet_active && factory == initial && !factory->is_xnet_backend()
                        && dynamic_cast<vsomeip_v3::asio_socket_factory*>(factory) != nullptr
                        && factory->get_xnet_ip_stack(io) == nullptr;
                std::exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
            },
            ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}
