#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>

#include <vsomeip/vsomeip.hpp>

// Include headers for XNET and XNET socket factory
#include "nxsocket.h"
#include "nixnet.h"
#include "xnet_socket_factory.hpp"

#include "xnet-stack-configuration.h"

std::shared_ptr<vsomeip::application> app;

void stop_application(int exit_code) {
    std::cout << "\nShutting down application..." << std::endl;

    // Stop the application and release the service and event
    if (app) {
        app->release_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID);
        app->release_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        app->stop();
    }
    std::exit(exit_code);
}

bool setup_xnet_stack() {
    nxIpStackRef_t xnet_stack = nullptr;

    // Initialize the xnet IP stack with the provided configuration
    nxStatus_t status{};
    status = nxIpStackCreate("xnet-notify-tp-client", client::xnet_ip_stack_config, &xnet_stack);
    if (status != 0) {
        std::cerr << "Failed to create XNET IP stack. Status code: " << status << std::endl;
        return false;
    }

    // Wait for the interface to be ready
    std::cout << "Waiting for XNET IP stack to be ready..." << std::endl;
    nxIpStackWaitForInterface(xnet_stack, client::xnet_interface_name, 30000);

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

void on_availability(vsomeip::service_t _service, vsomeip::instance_t _instance, bool _is_available) {
    if (_is_available) {
        // Request the event and subscribe to its eventgroup
        std::set<vsomeip::eventgroup_t> groups;
        groups.insert(SAMPLE_EVENTGROUP_ID);
        app->request_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, groups, vsomeip::event_type_e::ET_FIELD);
        app->subscribe(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENTGROUP_ID);
    }
}

void on_message(const std::shared_ptr<vsomeip::message>& _response) {
    // Extract the payload from the received message
    std::uint32_t counter;
    std::shared_ptr<vsomeip::payload> payload = _response->get_payload();
    std::memcpy(&counter, payload->get_data(), sizeof(counter));

    std::cout << "Received " << payload->get_length() << " bytes data: " << counter << std::endl;
}

bool setup_application() {
    // Create a vsomeip application
    app = vsomeip::runtime::get()->create_application("xnet-notify-tp-client");

    // Initialize the application
    if (!app->init()) {
        std::cerr << "Couldn't initialize application" << std::endl;
        return false;
    }

    // Register a callback which is called as soon as the service is available
    app->register_availability_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, on_availability);

    // Register a message handler callback for the received notifications
    app->register_message_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, on_message);

    // Request the service
    app->request_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

    return true;
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

    // Start the application and wait for incoming notifications
    app->start();

    return 0;
}
