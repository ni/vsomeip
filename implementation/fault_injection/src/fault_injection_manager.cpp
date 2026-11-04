// Copyright (C) 2025 NI
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../include/fault_injection_manager.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include <boost/asio/post.hpp>
#include <boost/filesystem.hpp>
#include <vsomeip/internal/logger.hpp>

#ifdef __linux__
#include <sys/inotify.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <thread>
#include <fcntl.h>
#include <chrono>
#include <poll.h>
#endif

namespace vsomeip_v3 {
namespace fault_injection {

fault_injection_manager::fault_injection_manager(boost::asio::io_context& _io_context) : io_context_(_io_context) { }

fault_injection_manager::~fault_injection_manager() {
    shutdown();
}

bool fault_injection_manager::initialize(const std::string& _config_file_path) {
    config_file_path_ = _config_file_path;

    boost::filesystem::path p(config_file_path_);
    config_filename_ = p.filename().string();

    if (!load_config_file()) {
        VSOMEIP_WARNING << "fault_injection_manager: Failed to load config from: " << config_file_path_;
    }

    timer_ = std::make_unique<boost::asio::steady_timer>(io_context_);
    monitoring_enabled_ = true;

    try {
        last_write_time_ = boost::filesystem::exists(config_file_path_) ? boost::filesystem::last_write_time(config_file_path_) : 0;
    } catch (...) {
        last_write_time_ = 0;
    }

#ifdef __linux__
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        VSOMEIP_WARNING << "fault_injection_manager: Failed to create pipe, config file is not monitored";
    } else {
        pipe_read_fd_ = pipefd[0];
        pipe_write_fd_ = pipefd[1];
        fcntl(pipe_read_fd_, F_SETFL, fcntl(pipe_read_fd_, F_GETFL, 0) | O_NONBLOCK);
        monitor_thread_ = std::make_unique<std::thread>(&fault_injection_manager::monitor_file_linux, this);
    }
#else
    monitor_file();
#endif
    VSOMEIP_INFO << "fault_injection_manager: Initialized with " << get_rules_count() << " rules";
    return true;
}

void fault_injection_manager::shutdown() {
    monitoring_enabled_ = false;
#ifdef __linux__
    // Signal the monitor thread to exit
    if (pipe_write_fd_ != -1) {
        char dummy = 'x';
        std::ignore = write(pipe_write_fd_, &dummy, 1);
    }
    if (monitor_thread_ && monitor_thread_->joinable()) {
        monitor_thread_->join();
    }
    monitor_thread_.reset();
    // Closed only after the join: the thread polls the read end, and vsomeip's close() wrapper terminates on EBADF.
    for (int* fd : {&pipe_read_fd_, &pipe_write_fd_}) {
        if (*fd != -1) {
            close(*fd);
            *fd = -1;
        }
    }
#else
    if (timer_) {
        boost::system::error_code ec;
        timer_->cancel(ec);
        timer_.reset();
    }
#endif
    std::lock_guard<std::mutex> lock(rules_mutex_);
    fault_rules_.clear();
}

std::uint32_t fault_injection_manager::get_fault_types(service_t _service, instance_t _instance, event_t _method_event) const {
    std::lock_guard<std::mutex> lock(rules_mutex_);

    // Priority lookup: exact > wildcard instance > wildcard method > both wildcards
    const rule_key_t keys[] = {
            rule_key_t(_service, _instance, _method_event), // exact match
            rule_key_t(_service, static_cast<instance_t>(0xFFFF), _method_event), // wildcard instance
            rule_key_t(_service, _instance, static_cast<event_t>(0xFFFF)), // wildcard method
            rule_key_t(_service, static_cast<instance_t>(0xFFFF), static_cast<event_t>(0xFFFF)) // both wildcards
    };

    for (const auto& key : keys) {
        auto it = fault_rules_.find(key);
        if (it != fault_rules_.end()) {
            return static_cast<std::uint32_t>(it->second.faults_);
        }
    }
    return FAULT_NONE;
}

bool fault_injection_manager::is_disabled(service_t _service, instance_t _instance, event_t _method_event) const {
    std::lock_guard<std::mutex> lock(rules_mutex_);

    // Priority lookup: exact > wildcard instance > wildcard method > both wildcards
    const rule_key_t keys[] = {
            rule_key_t(_service, _instance, _method_event), // exact match
            rule_key_t(_service, static_cast<instance_t>(0xFFFF), _method_event), // wildcard instance
            rule_key_t(_service, _instance, static_cast<event_t>(0xFFFF)), // wildcard method
            rule_key_t(_service, static_cast<instance_t>(0xFFFF), static_cast<event_t>(0xFFFF)) // both wildcards
    };

    for (const auto& key : keys) {
        auto it = fault_rules_.find(key);
        if (it != fault_rules_.end()) {
            return it->second.disabled_;
        }
    }
    return false; // Default: not disabled
}

std::size_t fault_injection_manager::get_rules_count() const {
    std::lock_guard<std::mutex> lock(rules_mutex_);
    return fault_rules_.size();
}

bool fault_injection_manager::load_config_file() {
    std::ifstream file(config_file_path_);
    if (!file.is_open()) {
        return false;
    }

    std::map<rule_key_t, fault_state_t> new_rules;
    std::string line;
    std::size_t line_number = 0;

    auto parse_value = [&](const std::string& _str) -> std::uint16_t {
        return static_cast<std::uint16_t>((_str.substr(0, 2) == "0x" || _str.substr(0, 2) == "0X") ? std::stoul(_str, nullptr, 16)
                                                                                                   : std::stoul(_str));
    };

    while (std::getline(file, line)) {
        ++line_number;
        std::istringstream iss(line);
        std::vector<std::string> tokens;
        std::string token;

        while (iss >> token && !token.empty() && token[0] != '#') {
            tokens.push_back(token);
        }

        if (tokens.size() < 3 || tokens.size() > 5) {
            continue; // Updated: now need at least 3 tokens
        }

        try {
            std::uint16_t disabled = parse_value(tokens[0]); // New: disabled field
            std::uint16_t fault_type = parse_value(tokens[1]); // Shifted: fault_type is now second
            std::uint16_t service_id = parse_value(tokens[2]); // Shifted: service_id is now third
            std::uint16_t instance_id = (tokens.size() >= 4) ? parse_value(tokens[3]) : 0xFFFF;
            std::uint16_t method_event_id = (tokens.size() >= 5) ? parse_value(tokens[4]) : 0xFFFF;

            rule_key_t key{service_id, instance_id, method_event_id};
            if (new_rules.find(key) != new_rules.end()) {
                VSOMEIP_WARNING << "fault_injection_manager: Duplicate rule at line " << line_number;
                continue;
            }

            fault_state_t state;
            state.disabled_ = (disabled != 0);
            state.faults_ = static_cast<fault_type_e>(fault_type);
            new_rules[key] = state;

        } catch (...) {
            VSOMEIP_WARNING << "fault_injection_manager: Invalid line " << line_number << ": " << line;
        }
    }

    std::lock_guard<std::mutex> lock(rules_mutex_);
    fault_rules_ = std::move(new_rules);
    VSOMEIP_INFO << "fault_injection_manager: Loaded " << fault_rules_.size() << " rules";
    return true;
}

void fault_injection_manager::monitor_file() {
    if (!monitoring_enabled_) {
        return;
    }

    try {
        if (boost::filesystem::exists(config_file_path_)) {
            auto current_time = boost::filesystem::last_write_time(config_file_path_);
            if (current_time != last_write_time_) {
                last_write_time_ = current_time;
                if (load_config_file()) {
                    VSOMEIP_INFO << "fault_injection_manager: Reloaded " << get_rules_count() << " rules";
                }
            }
        } else if (!fault_rules_.empty()) {
            std::lock_guard<std::mutex> lock(rules_mutex_);
            fault_rules_.clear();
            VSOMEIP_INFO << "fault_injection_manager: Config file deleted, cleared rules";
        }
    } catch (...) {
        // Ignore filesystem errors during monitoring
    }

    timer_->expires_after(CHECK_INTERVAL);
    timer_->async_wait([this](const boost::system::error_code& _error) {
        if (!_error) {
            monitor_file();
        }
    });
}

#ifdef __linux__
void fault_injection_manager::monitor_file_linux() {
    VSOMEIP_DEBUG << "fault_injection_manager: Monitor " << config_file_path_ << " using inotify";
    boost::filesystem::path p(config_file_path_);
    std::string dir = p.parent_path().string();
    if (dir.empty()) {
        dir = ".";
    }
    std::string filename = p.filename().string();

    const int pipe_read_fd = pipe_read_fd_;

    int fd = inotify_init();
    if (fd == -1) {
        VSOMEIP_WARNING << "fault_injection_manager: Failed to initialize inotify";
        return;
    }

    // Make the file descriptor non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int wd = inotify_add_watch(fd, dir.c_str(), IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO);
    if (wd == -1) {
        VSOMEIP_WARNING << "fault_injection_manager: Failed to add inotify watch on directory: " << dir;
        close(fd);
        return;
    }

    struct pollfd fds[2];
    fds[0].fd = fd;
    fds[0].events = POLLIN;
    fds[1].fd = pipe_read_fd;
    fds[1].events = POLLIN;

    char buffer[4096];
    while (monitoring_enabled_) {
        int ret = poll(fds, 2, -1); // Wait indefinitely
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (fds[0].revents & POLLIN) {
            // Inotify data available
            ssize_t len = read(fd, buffer, sizeof(buffer));
            if (len == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                break;
            }

            for (char* ptr = buffer; ptr < buffer + len;) {
                struct inotify_event* event = (struct inotify_event*)ptr;
                if (event->len > 0 && strcmp(event->name, filename.c_str()) == 0) {
                    if (event->mask & IN_CLOSE_WRITE) {
                        boost::asio::post(io_context_, [this]() {
                            if (load_config_file()) {
                                VSOMEIP_INFO << "fault_injection_manager: Reloaded " << get_rules_count() << " rules";
                            }
                        });
                    } else if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                        boost::asio::post(io_context_, [this]() {
                            if (load_config_file()) {
                                VSOMEIP_INFO << "fault_injection_manager: Reloaded " << get_rules_count() << " rules";
                            }
                        });
                    } else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                        boost::asio::post(io_context_, [this]() {
                            std::lock_guard<std::mutex> lock(rules_mutex_);
                            fault_rules_.clear();
                            VSOMEIP_INFO << "fault_injection_manager: Config file removed, cleared rules";
                        });
                    }
                }
                ptr += sizeof(struct inotify_event) + event->len;
            }
        }

        if (fds[1].revents & POLLIN) {
            // Pipe data available, consume it
            char dummy;
            std::ignore = read(pipe_read_fd, &dummy, 1);
            // Check monitoring_enabled_ again
        }
    }

    inotify_rm_watch(fd, wd);
    close(fd);
}
#endif

} // namespace fault_injection
} // namespace vsomeip_v3