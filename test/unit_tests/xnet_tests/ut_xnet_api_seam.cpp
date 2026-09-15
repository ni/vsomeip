#include <gtest/gtest.h>

#include <thread>

#include "xnet_api.hpp"
#include "xnet_error.hpp"
#include "common/xnet_test_utils.hpp"

namespace {
auto fake_last_error() {
    return static_cast<decltype(vsomeip_v3::xnet_api::nxgetlasterrornum())>(13850);
}
}

TEST(xnet_api_seam_test, injected_last_error_is_used_by_xnet_get_last_error) {
    vsomeip_v3::xnet_api::api_table table = vsomeip_v3::xnet_api::get_api_table();
    table.nxgetlasterrornum_fn = &fake_last_error;

    vsomeip_v3::xnet_api::set_api_table_for_test(table);

    EXPECT_EQ(vsomeip_v3::xnet_get_last_error(), 13850);

    vsomeip_v3::xnet_api::reset_api_table_for_test();
}

TEST(xnet_test_utils_test, async_completion_probe_waits_and_counts_signals) {
    using namespace std::chrono_literals;

    xnet_test_utils::async_completion_probe probe;

    std::thread worker([&probe]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        probe.signal();
    });

    ASSERT_TRUE(probe.wait_for(200ms, 1U));
    EXPECT_EQ(probe.signal_count(), 1U);

    worker.join();
}

TEST(xnet_test_utils_test, call_sequence_recorder_stores_ordered_calls) {
    xnet_test_utils::call_sequence_recorder recorder;
    recorder.push("nxsocket");
    recorder.push("nxbind");
    recorder.push("nxclose");

    auto calls = recorder.snapshot();
    ASSERT_EQ(calls.size(), 3U);
    EXPECT_EQ(calls[0], "nxsocket");
    EXPECT_EQ(calls[1], "nxbind");
    EXPECT_EQ(calls[2], "nxclose");
}