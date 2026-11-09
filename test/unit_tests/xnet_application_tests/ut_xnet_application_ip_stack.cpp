// Tests verifying the multiple XNET IP stack support at application level:
// - application::init(stack) / application::init(json, stack) register the IP stack
//   for the application io_context through the socket factory,
// - the endpoint manager of a routing host binds its auxiliary io_context to the
//   same IP stack,
// - destroying the application releases both registrations.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <vsomeip/vsomeip.hpp>

#include "application_impl.hpp"
#include "recording_socket_factory.hpp"

namespace {

nxIpStackRef_t fake_stack(std::uintptr_t _value) {
    return reinterpret_cast<nxIpStackRef_t>(_value);
}

std::string make_json(const std::string& _name, const std::string& _id, const std::string& _routing) {
    return R"json(
{
    "unicast": "127.0.0.1",
    "logging": { "level": "info", "console": "false", "dlt": "false" },
    "applications": [ { "name": ")json"
           + _name + R"json(", "id": ")json" + _id + R"json(" } ],
    "routing": ")json"
           + _routing + R"json(",
    "service-discovery": { "enable": "false" }
}
)json";
}

boost::asio::io_context& io_of(const std::shared_ptr<vsomeip::application>& _app) {
    auto impl = std::dynamic_pointer_cast<vsomeip_v3::application_impl>(_app);
    EXPECT_NE(impl, nullptr);
    return impl->get_io();
}

class xnet_application_test : public ::testing::Test {
protected:
    void SetUp() override { factory().clear(); }

    static recording_socket_factory& factory() { return get_recording_socket_factory(); }
};

}

TEST_F(xnet_application_test, init_with_null_stack_fails_and_registers_nothing) {
    auto app = vsomeip::runtime::get()->create_application("xnet_null_stack_app");
    ASSERT_NE(app, nullptr);

    EXPECT_FALSE(app->init(make_json("xnet_null_stack_app", "0x1301", "xnet_null_stack_app"), nullptr));
    EXPECT_TRUE(factory().stack_registrations().empty());
    EXPECT_TRUE(factory().auxiliary_registrations().empty());
}

TEST_F(xnet_application_test, init_stack_only_with_null_stack_fails_and_registers_nothing) {
    auto app = vsomeip::runtime::get()->create_application("xnet_null_stack_only_app");
    ASSERT_NE(app, nullptr);

    EXPECT_FALSE(app->init(static_cast<nxIpStackRef_t>(nullptr)));
    EXPECT_TRUE(factory().stack_registrations().empty());
    EXPECT_TRUE(factory().auxiliary_registrations().empty());
}

TEST_F(xnet_application_test, init_without_json_and_stack_fails_with_xnet_backend) {
    auto app = vsomeip::runtime::get()->create_application("xnet_no_json_no_stack_app");
    ASSERT_NE(app, nullptr);

    EXPECT_FALSE(app->init());
    EXPECT_EQ(factory().get_xnet_ip_stack(io_of(app)), nullptr);
}

TEST_F(xnet_application_test, init_without_stack_fails_with_xnet_backend) {
    auto app = vsomeip::runtime::get()->create_application("xnet_no_stack_app");
    ASSERT_NE(app, nullptr);

    EXPECT_FALSE(app->init(make_json("xnet_no_stack_app", "0x1302", "xnet_no_stack_app")));
    EXPECT_EQ(factory().get_xnet_ip_stack(io_of(app)), nullptr);
}

TEST_F(xnet_application_test, init_with_stack_registers_stack_for_application_io) {
    auto app = vsomeip::runtime::get()->create_application("xnet_init_stack_app");
    ASSERT_NE(app, nullptr);

    // Registration happens before the configuration is loaded, independent of the init() result.
    (void)app->init(fake_stack(0x10));

    auto const registrations = factory().stack_registrations();
    ASSERT_EQ(registrations.size(), 1U);
    EXPECT_EQ(registrations[0].io, &io_of(app));
    EXPECT_EQ(registrations[0].stack, fake_stack(0x10));
    EXPECT_EQ(factory().get_xnet_ip_stack(io_of(app)), fake_stack(0x10));
}

TEST_F(xnet_application_test, init_json_with_stack_registers_stack_for_application_io) {
    auto app = vsomeip::runtime::get()->create_application("xnet_json_stack_app");
    ASSERT_NE(app, nullptr);

    ASSERT_TRUE(app->init(make_json("xnet_json_stack_app", "0x1303", "xnet_json_stack_app"), fake_stack(0x20)));

    auto const registrations = factory().stack_registrations();
    ASSERT_EQ(registrations.size(), 1U);
    EXPECT_EQ(registrations[0].io, &io_of(app));
    EXPECT_EQ(registrations[0].stack, fake_stack(0x20));
    EXPECT_EQ(factory().get_xnet_ip_stack(io_of(app)), fake_stack(0x20));
}

TEST_F(xnet_application_test, routing_host_endpoint_manager_registers_auxiliary_io) {
    auto app = vsomeip::runtime::get()->create_application("xnet_routing_host_app");
    ASSERT_NE(app, nullptr);

    ASSERT_TRUE(app->init(make_json("xnet_routing_host_app", "0x1304", "xnet_routing_host_app"), fake_stack(0x30)));

    auto const auxiliaries = factory().auxiliary_registrations();
    ASSERT_EQ(auxiliaries.size(), 1U);
    EXPECT_EQ(auxiliaries[0].io, &io_of(app));
    EXPECT_NE(auxiliaries[0].auxiliary_io, &io_of(app));
    EXPECT_EQ(factory().get_xnet_ip_stack(auxiliaries[0].auxiliary_io), fake_stack(0x30));
}

TEST_F(xnet_application_test, client_application_does_not_register_auxiliary_io) {
    auto host = vsomeip::runtime::get()->create_application("xnet_host_app");
    ASSERT_NE(host, nullptr);
    ASSERT_TRUE(host->init(make_json("xnet_host_app", "0x1305", "xnet_host_app"), fake_stack(0x40)));
    factory().clear();

    auto client = vsomeip::runtime::get()->create_application("xnet_client_app");
    ASSERT_NE(client, nullptr);
    ASSERT_TRUE(client->init(make_json("xnet_client_app", "0x1306", "xnet_host_app"), fake_stack(0x50)));

    EXPECT_TRUE(factory().auxiliary_registrations().empty());
    EXPECT_EQ(factory().get_xnet_ip_stack(io_of(client)), fake_stack(0x50));
}

TEST_F(xnet_application_test, applications_in_one_process_keep_separate_stacks) {
    auto app_a = vsomeip::runtime::get()->create_application("xnet_multi_a");
    auto app_b = vsomeip::runtime::get()->create_application("xnet_multi_b");
    ASSERT_NE(app_a, nullptr);
    ASSERT_NE(app_b, nullptr);

    ASSERT_TRUE(app_a->init(make_json("xnet_multi_a", "0x1307", "xnet_multi_a"), fake_stack(0x60)));
    ASSERT_TRUE(app_b->init(make_json("xnet_multi_b", "0x1308", "xnet_multi_a"), fake_stack(0x70)));

    EXPECT_NE(&io_of(app_a), &io_of(app_b));
    EXPECT_EQ(factory().get_xnet_ip_stack(io_of(app_a)), fake_stack(0x60));
    EXPECT_EQ(factory().get_xnet_ip_stack(io_of(app_b)), fake_stack(0x70));
}

TEST_F(xnet_application_test, second_init_does_not_rebind_stack) {
    auto app = vsomeip::runtime::get()->create_application("xnet_reinit_app");
    ASSERT_NE(app, nullptr);
    auto const json = make_json("xnet_reinit_app", "0x1309", "xnet_reinit_app");

    ASSERT_TRUE(app->init(json, fake_stack(0x80)));
    EXPECT_TRUE(app->init(json, fake_stack(0x90)));
    EXPECT_TRUE(app->init(fake_stack(0x91)));

    EXPECT_EQ(factory().stack_registrations().size(), 1U);
    EXPECT_EQ(factory().get_xnet_ip_stack(io_of(app)), fake_stack(0x80));
}

TEST_F(xnet_application_test, destroying_application_unregisters_io_and_auxiliary_io) {
    const boost::asio::io_context* app_io = nullptr;
    const boost::asio::io_context* auxiliary_io = nullptr;
    {
        auto app = vsomeip::runtime::get()->create_application("xnet_teardown_app");
        ASSERT_NE(app, nullptr);
        ASSERT_TRUE(app->init(make_json("xnet_teardown_app", "0x130A", "xnet_teardown_app"), fake_stack(0xA0)));

        app_io = &io_of(app);
        auto const auxiliaries = factory().auxiliary_registrations();
        ASSERT_EQ(auxiliaries.size(), 1U);
        auxiliary_io = auxiliaries[0].auxiliary_io;
    }

    auto const unregistrations = factory().unregistrations();
    EXPECT_NE(std::find(unregistrations.begin(), unregistrations.end(), app_io), unregistrations.end());
    EXPECT_NE(std::find(unregistrations.begin(), unregistrations.end(), auxiliary_io), unregistrations.end());
}
