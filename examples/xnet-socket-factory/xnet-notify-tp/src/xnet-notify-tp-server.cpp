#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "nxsocket.h"
#include "nixnet.h"
#include "xnet_socket_factory.hpp"

#include <vsomeip/vsomeip.hpp>

#include "xnet-notify-tp-server.h"

std::shared_ptr<vsomeip::application> app;
static nxIpStackRef_t g_xnet_stack = nullptr;
static std::atomic_bool running{true};
static std::thread notify_thread;

void stop_application(int exit_code) {
    std::cout << "\nShutting down application..." << std::endl;
    running = false;
    if (notify_thread.joinable()) {
        notify_thread.join();
    }
    if (app) {
        app->stop_offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        app->stop();
    }
    std::exit(exit_code);
}

// Total size of the notification payload. It exceeds the UDP-SOMEIP/TP
// segmentation threshold, so the message is sent as several TP segments.
constexpr std::size_t MESSAGE_LENGTH = 1000;

// Builds a MESSAGE_LENGTH byte message of the form:
// "<START #n>####...####<END #n>"
static std::vector<vsomeip::byte_t> build_message(std::uint32_t counter) {
    const std::string start = "<START #" + std::to_string(counter) + ">";
    const std::string end = "<END #" + std::to_string(counter) + ">";

    std::string message = start;
    if (start.size() + end.size() < MESSAGE_LENGTH) {
        message.append(MESSAGE_LENGTH - start.size() - end.size(), '#');
    }
    message.append(end);
    message.resize(MESSAGE_LENGTH, '#');

    return std::vector<vsomeip::byte_t>(message.begin(), message.end());
}

void notify() {
    std::uint32_t timer = 0;
    std::shared_ptr<vsomeip::payload> payload = vsomeip::runtime::get()->create_payload();

    while (running) {
        const std::vector<vsomeip::byte_t> data = build_message(timer);
        payload->set_data(data);

        app->notify(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, payload);

        std::cout << "Notifying #" << timer++ << " with " << data.size() << " bytes" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

int main() {
    // Initialize the xnet IP stack with the provided configuration
    nxStatus_t status{};
    status = nxIpStackCreate("xnet-notify-tp-server", xnet_ip_stack_config, &g_xnet_stack);
    if (status != 0) {
        std::cerr << "Failed to create XNET IP stack. Status code: " << status << std::endl;
        return 1;
    }

    // Wait for the interface to be ready
    std::cout << "Waiting for XNET IP stack to be ready..." << std::endl;
    nxIpStackWaitForInterface(g_xnet_stack, "ENET1", 30000); // Wait for the interface to be ready (30 seconds timeout)

    // Get and print the actual stack information
    char* ip_stack_info = nullptr;
    status = nxIpStackGetAllStacksInfoStr(nxIPSTACK_INFO_STR_FORMAT_JSON, &ip_stack_info);
    if (status != 0) {
        std::cerr << "Failed to get XNET IP stack info. Status code: " << status << std::endl;
        return 1;
    }

    std::cout << "XNET IP Stack Information:" << std::endl;
    std::cout << ip_stack_info << std::endl;
    nxIpStackFreeAllStacksInfoStr(ip_stack_info);

    try {
        // Create factory with XNET driver enabled (true) or disabled (false)

        std::cout << "Initializing XNET socket factory with XNET driver enabled..." << std::endl;
        auto xnet_factory = std::make_shared<vsomeip_v3::xnet_socket_factory>(g_xnet_stack);
        vsomeip_v3::set_abstract_factory(xnet_factory);

    } catch (std::exception const& e) {
        std::cerr << "Failed to initialize XNET socket factory: " << e.what() << std::endl;
        return 1;
    }

    // create a vsomeip application
    app = vsomeip::runtime::get()->create_application("xnet-notify-tp-server");
    
    // initialize the application
    if (!app->init()) {
        std::cerr << "Couldn't initialize application" << std::endl;
        return 1;
    }

    app->offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

    // offer the event
    std::set<vsomeip::eventgroup_t> its_groups;
    its_groups.insert(SAMPLE_EVENTGROUP_ID);
    app->offer_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, its_groups);

    // Register signal handler for clean shutdown
    std::signal(SIGINT, stop_application);
    std::signal(SIGTERM, stop_application);

    // Start sending cyclic notifications
    notify_thread = std::thread(notify);

    // Start the application and wait for incoming messages
    app->start();

    stop_application(0);
}
