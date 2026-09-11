#include <csignal>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "nxsocket.h"
#include "nixnet.h"
#include "xnet_socket_factory.hpp"

#include <vsomeip/vsomeip.hpp>

#include "xnet-notify-client.h"

std::shared_ptr<vsomeip::application> app;
static nxIpStackRef_t g_xnet_stack = nullptr;

void stop_application(int exit_code) {
    std::cout << "\nShutting down application..." << std::endl;
    if (app) {
        app->stop_offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        // Properly stop vsomeip application to ensure all resources are released correctly
        std::cout << "Stopping vsomeip application..." << std::endl;
        app->stop();
    }
    std::exit(exit_code);
}

void on_availability(vsomeip::service_t _service, vsomeip::instance_t _instance, bool _is_available) {
    if (_is_available) {
        std::set<vsomeip::eventgroup_t> groups;
        groups.insert(SAMPLE_EVENTGROUP_ID);
        app->request_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, groups);
        app->subscribe(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENTGROUP_ID);
    }
}

void on_message(const std::shared_ptr<vsomeip::message>& _response) {
    std::shared_ptr<vsomeip::payload> payload = _response->get_payload();
    std::uint32_t value;
    std::memcpy(&value, payload->get_data(), sizeof(value));
    std::cout << "Received: " << value << std::endl;
}

int main() {
    // Initialize the xnet IP stack with the provided configuration
    nxStatus_t status{};
    status = nxIpStackCreate("xnet-notify-client", xnet_ip_stack_config, &g_xnet_stack);
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
    app = vsomeip::runtime::get()->create_application("xnet-notify-client");

    // initialize the application
    if (!app->init()) {
        std::cerr << "Couldn't initialize application" << std::endl;
        return 1;
    }

    // register a callback which is called as soon as the service is available
    app->register_availability_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, on_availability);

    app->request_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

    // register a message handler callback for received notifications
    app->register_message_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, on_message);

    // Register signal handler for clean shutdown
    std::signal(SIGINT, stop_application);
    std::signal(SIGTERM, stop_application);

    // start the application
    app->start();
}
