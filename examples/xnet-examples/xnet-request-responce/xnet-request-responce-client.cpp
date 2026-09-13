#include <csignal>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <vsomeip/vsomeip.hpp>

// Include headers for XNET and XNET socket factory
#include "nxsocket.h"
#include "nixnet.h"
#include "xnet_socket_factory.hpp"

#include "xnet-stack-configuration.h"

std::shared_ptr<vsomeip::application> app;

void stop_application(int exit_code) {
    std::cout << "\nShutting down application..." << std::endl;

    // Release the service and stop the application
    if (app) {
        app->release_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        app->stop();
    }
    std::exit(exit_code);
}

bool setup_xnet_stack() {
    nxIpStackRef_t xnet_stack = nullptr;

    // Initialize the xnet IP stack with the provided configuration
    nxStatus_t status{};
    status = nxIpStackCreate("xnet-request-responce-client", client::xnet_ip_stack_config, &xnet_stack);
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
    if (_service == SAMPLE_SERVICE_ID && _instance == SAMPLE_INSTANCE_ID && _is_available) {
        // Send a request to the server when it becomes available
        const std::string request_text = "XNET Request";

        // Create a payload for the request
        std::shared_ptr<vsomeip::payload> payload = vsomeip::runtime::get()->create_payload();
        std::vector<vsomeip::byte_t> payload_data(request_text.begin(), request_text.end());
        payload->set_data(payload_data);

        // Create a request message
        std::shared_ptr<vsomeip::message> request = vsomeip::runtime::get()->create_request();
        request->set_service(SAMPLE_SERVICE_ID);
        request->set_instance(SAMPLE_INSTANCE_ID);
        request->set_method(SAMPLE_METHOD_ID);
        request->set_payload(payload);

        // Send the request
        app->send(request);

        std::cout << "Sending: " << request_text << std::endl;
    }
}

void on_message(const std::shared_ptr<vsomeip::message>& _response) {
    // Parse the received message
    std::shared_ptr<vsomeip::payload> response_payload = _response->get_payload();

    std::string received_text(reinterpret_cast<const char*>(response_payload->get_data()), response_payload->get_length());

    std::cout << "Received: " << received_text << std::endl;

    // Stop the application
    stop_application(0);
}

bool setup_application() {
    // Create a vsomeip application
    app = vsomeip::runtime::get()->create_application("xnet-request-responce-client");

    // Initialize the application
    if (!app->init()) {
        std::cerr << "Couldn't initialize application" << std::endl;
        return false;
    }

    // Register a message handler callback for the received messages
    app->register_message_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_METHOD_ID, on_message);

    // Register a callback which is called as soon as the service is available
    app->register_availability_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, on_availability);

    // Request the remote service
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

    // Start the application and wait for incoming messages
    app->start();

    return 0;
}
