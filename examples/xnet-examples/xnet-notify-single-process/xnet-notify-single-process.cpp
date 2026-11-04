#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>

#include <vsomeip/vsomeip.hpp>

// Include headers for XNET and XNET socket factory
#include "nxsocket.h"
#include "nixnet.h"
#include "xnet_socket_factory.hpp"

#include "xnet-stack-configuration.h"

// Both applications live in this process. Each one uses its own "network"
// name, so each becomes the routing manager host of its own network and is
// bound to its own XNET IP stack. All traffic between them goes over XNET.

namespace server {
const char* app_name = "xnet-notify-server";
const char* vsomeip_config = R"(
{
  "unicast": "10.0.0.2",
  "network": "xnet-notify-server",
  "logging": { "level": "debug", "console": "true" },
  "applications": [ { "name": "xnet-notify-server", "id": "0x1212" } ],
  "services": [
    {
      "service": "0x1234",
      "instance": "0x5678",
      "unreliable": "30509",
      "events": [ { "event": "0x8778", "is_field": "true", "is_reliable": "false" } ],
      "eventgroups": [ { "eventgroup": "0x4465", "events": [ "0x8778" ] } ]
    }
  ],
  "routing": "xnet-notify-server",
  "service-discovery": {
    "enable": "true",
    "multicast": "224.224.224.245",
    "port": "30490",
    "protocol": "udp",
    "initial_delay_min": "10",
    "initial_delay_max": "100",
    "repetitions_base_delay": "200",
    "repetitions_max": "3",
    "ttl": "3",
    "cyclic_offer_delay": "2000",
    "request_response_delay": "1500"
  }
}
)";
}

namespace client {
const char* app_name = "xnet-notify-client";
const char* vsomeip_config = R"(
{
  "unicast": "10.0.0.3",
  "network": "xnet-notify-client",
  "logging": { "level": "debug", "console": "true" },
  "applications": [ { "name": "xnet-notify-client", "id": "0x1313" } ],
  "routing": "xnet-notify-client",
  "service-discovery": {
    "enable": "true",
    "multicast": "224.224.224.245",
    "port": "30490",
    "protocol": "udp",
    "initial_delay_min": "10",
    "initial_delay_max": "100",
    "repetitions_base_delay": "200",
    "repetitions_max": "3",
    "ttl": "3",
    "cyclic_offer_delay": "2000",
    "request_response_delay": "1500"
  }
}
)";
}

std::shared_ptr<vsomeip::application> server_app;
std::shared_ptr<vsomeip::application> client_app;
nxIpStackRef_t server_stack = nullptr;
nxIpStackRef_t client_stack = nullptr;
std::atomic_bool running{true};
std::thread notify_thread;
std::thread client_thread;

void stop_application([[maybe_unused]] int exit_code) {
    std::cout << "\nShutting down applications..." << std::endl;

    // Stop the notification thread
    running = false;
    if (notify_thread.joinable()) {
        notify_thread.join();
    }

    if (client_app) {
        client_app->release_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID);
        client_app->release_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        client_app->stop();
    }

    if (server_app) {
        server_app->stop_offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        server_app->stop_offer_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID);
        server_app->stop();
    }
}

bool create_xnet_stack(const char* _name, const char* _config, const char* _interface, nxIpStackRef_t& _stack) {
    nxStatus_t status = nxIpStackCreate(_name, _config, &_stack);
    if (status != 0) {
        std::cerr << "Failed to create XNET IP stack \"" << _name << "\". Status code: " << status << std::endl;
        return false;
    }

    std::cout << "Waiting for XNET IP stack \"" << _name << "\" to be ready..." << std::endl;
    nxIpStackWaitForInterface(_stack, _interface, 30000);
    return true;
}

bool setup_xnet() {
    if (!create_xnet_stack("xnet-notify-server", server::xnet_ip_stack_config, server::xnet_interface_name, server_stack)) {
        return false;
    }
    if (!create_xnet_stack("xnet-notify-client", client::xnet_ip_stack_config, client::xnet_interface_name, client_stack)) {
        return false;
    }

    // One socket factory for the whole process. Each application binds its own
    // IP stack to it through 
    // ().
    try {
        std::cout << "Initializing XNET socket factory with XNET driver enabled..." << std::endl;
        auto xnet_factory = std::make_shared<vsomeip_v3::xnet_socket_factory>();
        vsomeip_v3::set_abstract_factory(xnet_factory);
    } catch (std::exception const& e) {
        std::cerr << "Failed to initialize XNET socket factory: " << e.what() << std::endl;
        return false;
    }
    return true;
}

std::shared_ptr<vsomeip::application> create_app(const char* _name, const char* _config, nxIpStackRef_t _stack) {
    auto its_app = vsomeip::runtime::get()->create_application(_name);

    // Initialize the application on its XNET IP stack from the in-memory JSON configuration
    if (!its_app->init(std::string(_config), _stack)) {
        std::cerr << "Couldn't initialize application \"" << _name << "\"" << std::endl;
        return nullptr;
    }
    return its_app;
}

bool setup_server() {
    server_app = create_app(server::app_name, server::vsomeip_config, server_stack);
    if (!server_app) {
        return false;
    }

    std::set<vsomeip::eventgroup_t> its_groups;
    its_groups.insert(SAMPLE_EVENTGROUP_ID);
    server_app->offer_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, its_groups);
    server_app->offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
    return true;
}

void on_availability([[maybe_unused]] vsomeip::service_t _service, [[maybe_unused]] vsomeip::instance_t _instance, bool _is_available) {
    if (_is_available) {
        std::set<vsomeip::eventgroup_t> groups;
        groups.insert(SAMPLE_EVENTGROUP_ID);
        client_app->request_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, groups, vsomeip::event_type_e::ET_FIELD);
        client_app->subscribe(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENTGROUP_ID);
    }
}

void on_message(const std::shared_ptr<vsomeip::message>& _response) {
    std::uint32_t data{};
    std::shared_ptr<vsomeip::payload> payload = _response->get_payload();
    if (payload->get_length() >= sizeof(data)) {
        std::memcpy(&data, payload->get_data(), sizeof(data));
    }
    std::cout << "[client] Received: " << data << std::endl;
}

bool setup_client() {
    client_app = create_app(client::app_name, client::vsomeip_config, client_stack);
    if (!client_app) {
        return false;
    }

    client_app->register_availability_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, on_availability);
    client_app->register_message_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, on_message);
    client_app->request_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
    return true;
}

void notify() {
    std::uint32_t counter = 0;
    std::shared_ptr<vsomeip::payload> payload = vsomeip::runtime::get()->create_payload();

    while (running) {
        payload->set_data(reinterpret_cast<const vsomeip::byte_t*>(&counter), sizeof(counter));
        server_app->notify(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, payload);
        std::cout << "[server] Notifying: " << counter++ << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main() {
    if (!setup_xnet()) {
        return 1;
    }

    if (!setup_server() || !setup_client()) {
        return 1;
    }

    std::signal(SIGINT, stop_application);
    std::signal(SIGTERM, stop_application);

    // start() blocks, so the client runs on its own thread
    client_thread = std::thread([] { client_app->start(); });
    notify_thread = std::thread(notify);

    server_app->start();

    if (client_thread.joinable()) {
        client_thread.join();
    }

    return 0;
}
