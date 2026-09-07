// Example: How to inject XNET socket factory into vsomeip

#include <iostream>
#include <csignal>

#include <vsomeip/vsomeip.hpp>

// Include the necessary headers for XNET and the XNET socket factory
#include "nxsocket.h"
#include "nixnet.h"
#include "xnet_socket_factory.hpp"


#define SAMPLE_SERVICE_ID 0x1234
#define SAMPLE_INSTANCE_ID 0x5678
#define SAMPLE_METHOD_ID 0x0421

std::shared_ptr<vsomeip::application> app;
static nxIpStackRef_t g_xnet_stack = nullptr;


// Create a dummy XNET IP stack
const char* config = R"(
{
    "schema":  "file:///NIXNET_Documentation/xnetIpStackSchema-07.json",
    "xnetInterfaces":  [
                           {
                               "name":  "ENET1",
                               "loopbackMode":  "externalAndInternal",
                               "MACs":  [
                                            {
                                                "address":  "generated",
                                                "VLANs":  [
                                                              {
                                                                  "IPv4":  {
                                                                               "mode":  "static",
                                                                               "staticAddresses":  [
                                                                                                       {
                                                                                                           "address":  "10.0.0.2",
                                                                                                           "subnetMask":  "255.255.255.0"
                                                                                                       }
                                                                                                   ]
                                                                           }
                                                              }
                                                          ]
                                            }
                                        ]
                           }
                       ]
}
)";

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

int main() {

    // Initialize the xnet IP stack with the provided configuration
    nxStatus_t status{};
    status = nxIpStackCreate("XnetExampleApp", config, &g_xnet_stack);
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

    app = vsomeip::runtime::get()->create_application("XnetExampleApp");

    // Init vsomeip application (loads configuration, initializes routing, etc.)
    if (!app->init()) {
        std::cerr << "Failed to initialize vsomeip application" << std::endl;
        return 1;
    }

    app->offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

    // Register signal handler for clean shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Start vsomeip application (blocking call)
    std::cout << "Starting vsomeip application..." << std::endl;
    app->start();

    return 0;
}