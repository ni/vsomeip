#include <csignal>
#include <iomanip>
#include <iostream>
#include <memory>
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

    // Stop offering the service stop the application
    if (app) {
        app->stop_offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        app->stop();
    }
    std::exit(exit_code);
}

bool setup_xnet_stack() {
    nxIpStackRef_t xnet_stack = nullptr;

    // Initialize the xnet IP stack with the provided configuration
    nxStatus_t status{};
    status = nxIpStackCreate("xnet-request-response-server", server::xnet_ip_stack_config, &xnet_stack);
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

void on_message(const std::shared_ptr<vsomeip::message>& _request) {
    // Parse the received message
    std::shared_ptr<vsomeip::payload> request_payload = _request->get_payload();

    std::string received_text(reinterpret_cast<const char*>(request_payload->get_data()), request_payload->get_length());

    std::cout << "Received from " << std::setw(4) << std::setfill('0') << std::hex
              << _request->get_client() << ": " << received_text << std::endl;

    // Send a response back
    const std::string response_text = "XNET Response";

    std::shared_ptr<vsomeip::message> response = vsomeip::runtime::get()->create_response(_request);
    std::shared_ptr<vsomeip::payload> response_payload = vsomeip::runtime::get()->create_payload();
    std::vector<vsomeip::byte_t> response_payload_data(response_text.begin(), response_text.end());

    response_payload->set_data(response_payload_data);
    response->set_payload(response_payload);

    app->send(response);

    std::cout << "Sending: " << response_text << std::endl;
}

bool setup_application() {
    // Create a vsomeip application
    app = vsomeip::runtime::get()->create_application("xnet-request-response-server");

    // Initialize the application
    if (!app->init()) {
        std::cerr << "Couldn't initialize application" << std::endl;
        return false;
    }

    // Register a message handler callback for the received messages
    app->register_message_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_METHOD_ID, on_message);

    // Offer the service
    app->offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

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
