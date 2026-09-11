#include <csignal>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "nxsocket.h"
#include "nixnet.h"
#include "xnet_socket_factory.hpp"

#include <vsomeip/vsomeip.hpp>

#include "xnet-request-client.h"

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

void on_availability(vsomeip::service_t _service, vsomeip::instance_t _instance, bool _is_available) {
    if (_service == SAMPLE_SERVICE_ID && _instance == SAMPLE_INSTANCE_ID && _is_available) {
        // send a request to the server when it becomes available
        const std::string request_text = "XNET Request";

        std::shared_ptr<vsomeip::message> request = vsomeip::runtime::get()->create_request();
        request->set_service(SAMPLE_SERVICE_ID);
        request->set_instance(SAMPLE_INSTANCE_ID);
        request->set_method(SAMPLE_METHOD_ID);

        std::shared_ptr<vsomeip::payload> payload = vsomeip::runtime::get()->create_payload();
        std::vector<vsomeip::byte_t> payload_data(request_text.begin(), request_text.end());
        payload->set_data(payload_data);
        request->set_payload(payload);

        app->send(request);

        std::cout << "Sending: " << request_text << std::endl;
    }
}

void on_message(const std::shared_ptr<vsomeip::message>& _response) {
    // parse the received message
    std::shared_ptr<vsomeip::payload> response_payload = _response->get_payload();

    std::string received_text(reinterpret_cast<const char*>(response_payload->get_data()), response_payload->get_length());

    std::cout << "Received: " << received_text << std::endl;

    // stop the application
    app->release_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
    app->stop();
}

int main() {
    // Initialize the xnet IP stack with the provided configuration
    nxStatus_t status{};
    status = nxIpStackCreate("xnet-request-responce-client", xnet_ip_stack_config, &g_xnet_stack);
    if (status != 0) {
        std::cerr << "Failed to create XNET IP stack. Status code: " << status << std::endl;
        return 1;
    }

    // Wait for the interface to be ready
    std::cout << "Waiting for XNET IP stack to be ready..." << std::endl;
    nxIpStackWaitForInterface(g_xnet_stack, "ENET2", 30000); // Wait for the interface to be ready (30 seconds timeout)

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
    app = vsomeip::runtime::get()->create_application("xnet-request-responce-client");

    // initialize the application
    if (!app->init()) {
        std::cerr << "Couldn't initialize application" << std::endl;
        return 1;
    }

    // register a message handler callback for responses from the server
    app->register_message_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_METHOD_ID, on_message);

    // register a callback which is called as soon as the server is available
    app->register_availability_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, on_availability);

    // request the server
    app->request_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

    // Register signal handler for clean shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // start the application
    app->start();
}
