// --- NI modification: BEGIN ---
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include <boost/asio/io_context.hpp>

#include "../../../implementation/fault_injection/include/fault_injection_manager.hpp"

using vsomeip_v3::fault_injection::fault_injection_manager;

// vsomeip's close() wrapper terminates the process on EBADF, so any double close during shutdown fails this test.
TEST(fault_injection_manager_test, shutdown_after_initialize_closes_each_descriptor_once) {
    boost::asio::io_context its_io;
    const auto its_config = std::filesystem::temp_directory_path() / "vsomeip_fault_injection_test_missing.json";

    for (int i = 0; i < 3; ++i) {
        fault_injection_manager its_manager(its_io);
        ASSERT_TRUE(its_manager.initialize(its_config.string()));
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // let the monitor thread reach poll(), as in a running application
        its_manager.shutdown();
        its_manager.shutdown(); // the destructor calls it again as well
    }
}
// --- NI modification: END ---
