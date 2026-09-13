#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <vector>
#include <thread>

#include <vsomeip/vsomeip.hpp>

// Include headers for XNET and XNET socket factory
#include "nxsocket.h"
#include "nixnet.h"
#include "xnet_socket_factory.hpp"

#include "xnet-stack-configuration.h"

std::shared_ptr<vsomeip::application> app;
std::atomic_bool running{true};
std::thread notify_thread;

void stop_application(int exit_code) {
    std::cout << "\nShutting down application..." << std::endl;

    // Stop the notification thread
    running = false;
    if (notify_thread.joinable()) {
        notify_thread.join();
    }

    // Stop offering the service and event, and stop the application
    if (app) {
        app->stop_offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        app->stop_offer_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID);
        app->stop();
    }
    std::exit(exit_code);
}

bool setup_xnet_stack() {
    nxIpStackRef_t xnet_stack = nullptr;

    // Initialize the xnet IP stack with the provided configuration
    nxStatus_t status{};
    status = nxIpStackCreate("xnet-notify-tp-server", server::xnet_ip_stack_config, &xnet_stack);
    if (status != 0) {
        std::cerr << "Failed to create XNET IP stack. Status code: " << status << std::endl;
        return false;
    }

    // Wait for the interface to be ready
    std::cout << "Waiting for XNET IP stack to be ready..." << std::endl;
    nxIpStackWaitForInterface(xnet_stack, server::xnet_interface_name, 30000);

    // Initialize the XNET socket factory
    try {
        std::cout << "Initializing XNET socket factory with XNET driver enabled..." << std::endl;
        auto xnet_factory = std::make_shared<vsomeip_v3::xnet_socket_factory>(xnet_stack);
        vsomeip_v3::set_abstract_factory(xnet_factory);

    } catch (std::exception const& e) {
        std::cerr << "Failed to initialize XNET socket factory: " << e.what() << std::endl;
        return false;
    }
    return true;
}

bool setup_application() {
    // Create a vsomeip application
    app = vsomeip::runtime::get()->create_application("xnet-notify-tp-server");

    // Initialize the application
    if (!app->init()) {
        std::cerr << "Couldn't initialize application" << std::endl;
        return false;
    }

    // Offer the event
    std::set<vsomeip::eventgroup_t> its_groups;
    its_groups.insert(SAMPLE_EVENTGROUP_ID);
    app->offer_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, its_groups);

    // Offer the service
    app->offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

    return true;
}

void notify() {
    // Initialize a vector of uint32_t with 40000 bytes of data
    std::vector<uint32_t> data(1000, 0);
    
    // Create a payload for the notification
    std::shared_ptr<vsomeip::payload> payload = vsomeip::runtime::get()->create_payload();

    while (running) {
        // Initialize the payload and send the notification
        payload->set_data(reinterpret_cast<const vsomeip::byte_t*>(data.data()),
                          static_cast<vsomeip::length_t>(data.size() * sizeof(data[0])));
        app->notify(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, payload);
        
        std::cout << "Notifying " << data.size() * sizeof(data[0]) << " bytes data: " << data[0] << std::endl;
        
        // Increment the first element (counter)
        ++data[0];

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main() {
    // Setup the XNET IP stack and socket factory
    if (!setup_xnet_stack()) {
        return 1;
    }

    // Setup the vsomeip application
    if (!setup_application()) {
        return 1;
    }

    // Register signal handler for clean shutdown
    std::signal(SIGINT, stop_application);
    std::signal(SIGTERM, stop_application);

    // Start sending cyclic notifications
    notify_thread = std::thread(notify);

    // Start the application and wait for incoming messages
    app->start();

    return 0;
}
