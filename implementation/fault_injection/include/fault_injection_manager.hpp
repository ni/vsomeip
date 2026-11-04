// Copyright (C) 2025 NI
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_FAULT_INJECTION_MANAGER_HPP
#define VSOMEIP_V3_FAULT_INJECTION_MANAGER_HPP

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>

#ifdef __linux__
#include <thread>
#endif

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "fault_injection_interface.hpp"

namespace vsomeip_v3 {
namespace fault_injection {

/**
 * @brief Fault injection manager with file monitoring and fast rule lookup
 */
class fault_injection_manager : public fault_injection_interface {
public:
    explicit fault_injection_manager(boost::asio::io_context& _io_context);
    ~fault_injection_manager();

    bool initialize(const std::string& _config_file_path);
    void shutdown();

    // Interface implementation
    std::uint32_t get_fault_types(service_t _service, instance_t _instance, event_t _method_event) const override;
    bool is_disabled(service_t _service, instance_t _instance, event_t _method_event) const override;

    std::size_t get_rules_count() const;

private:
    using rule_key_t = std::tuple<service_t, instance_t, event_t>;

    boost::asio::io_context& io_context_;
    std::string config_file_path_;
    std::string config_filename_;
    std::atomic<bool> monitoring_enabled_{false};
    std::unique_ptr<boost::asio::steady_timer> timer_;
    std::filesystem::file_time_type last_write_time_{};

    mutable std::mutex rules_mutex_;
    std::map<rule_key_t, fault_state_t> fault_rules_; // Store fault states

#ifdef __linux__
    std::unique_ptr<std::thread> monitor_thread_;
    // Shutdown signal for the monitor thread; created before and closed after the thread, by the owning thread only.
    int pipe_read_fd_ = -1;
    int pipe_write_fd_ = -1;
#endif

    bool load_config_file();
    void monitor_file();
#ifdef __linux__
    void monitor_file_linux();
#endif
    static constexpr auto CHECK_INTERVAL = std::chrono::seconds(1);
};

} // namespace fault_injection
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_FAULT_INJECTION_MANAGER_HPP