#include <csignal>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "nxsocket.h"
#include "nixnet.h"
#include "xnet_socket_factory.hpp"

#include <vsomeip/vsomeip.hpp>

#include "xnet-response-server.h"

#define SAMPLE_SERVICE_ID   0x1234
#define SAMPLE_INSTANCE_ID  0x5678
#define SAMPLE_METHOD_ID    0x0421

std::shared_ptr<vsomeip::application> app;
static nxIpStackRef_t g_xnet_stack = nullptr;

void signal_handler(int signum) {
    std::cout << "\nShutting down application..." << std::endl;
    if (app) {
        app->stop_offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        // Properly stop vsomeip application to ensure all resources are released correctly
        std::cout << "Stopping vsomeip application..." << std::endl;
        app->stop();
    }
    std::exit(signum);
}

void on_message(const std::shared_ptr<vsomeip::message>& _request) {
    // sarse the received message
    std::shared_ptr<vsomeip::payload> request_payload = _request->get_payload();

    std::string received_text(reinterpret_cast<const char*>(request_payload->get_data()), request_payload->get_length());

    std::cout << "Received from " << std::setw(4) << std::setfill('0') << std::hex
              << _request->get_client() << ": " << received_text << std::endl;

    // send an answer back to the client
    const std::string response_text = "XNET Response";

    std::shared_ptr<vsomeip::message> response = vsomeip::runtime::get()->create_response(_request);
    std::shared_ptr<vsomeip::payload> response_payload = vsomeip::runtime::get()->create_payload();
    std::vector<vsomeip::byte_t> response_payload_data(response_text.begin(), response_text.end());

    response_payload->set_data(response_payload_data);
    response->set_payload(response_payload);

    app->send(response);

    std::cout << "Sending: " << response_text << std::endl;
}

int main() {
    // Initialize the xnet IP stack with the provided configuration
    nxStatus_t status{};
    status = nxIpStackCreate("xnet-request-responce-server", xnet_ip_stack_config, &g_xnet_stack);
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
    app = vsomeip::runtime::get()->create_application("xnet-request-responce-server");
    
    // initialize the application
    if (!app->init()) {
        std::cerr << "Couldn't initialize application" << std::endl;
        return 1;
    }

    // register a message handler callback for messages from the client
    app->register_message_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_METHOD_ID, on_message);
    
    // start offering the service
    app->offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

    // Register signal handler for clean shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Start the application and wait for incoming messages
    app->start();

    return (0);
}
