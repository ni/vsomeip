#include <atomic>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <thread>

#include <vsomeip/vsomeip.hpp>
#include <vsomeip/xnet.hpp>
#include "nxsocket.h"
#include "nixnet.h"

#include "xnet-stack-configuration.h"

std::shared_ptr<vsomeip::application> client_app;
std::shared_ptr<vsomeip::application> server_app;
std::atomic_bool running{true};
std::thread notify_thread;
std::thread client_thread;
const char* server_app_name = "xnet-notify-server";
const char* client_app_name = "xnet-notify-client";

void stop_application([[maybe_unused]] int exit_code) {
    std::cout << "\nShutting down application..." << std::endl;

    // Stop the notification thread
    running = false;
    if (notify_thread.joinable()) {
        notify_thread.join();
    }

    // Stop the server application, stopping offering the service and event
    if (server_app) {
        server_app->stop_offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        server_app->stop_offer_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID);
        server_app->stop();
    }

    // Stop the client application, releasing the service and event
    if (client_app) {
        client_app->release_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID);
        client_app->release_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        client_app->stop();
    }
}

bool create_xnet_stack(const char* name, const char* config, const char* interface, nxIpStackRef_t& stack) {
    nxStatus_t status = nxIpStackCreate(name, config, &stack);
    if (status != 0) {
        std::cerr << "Failed to create XNET IP stack \"" << name << "\". Status code: " << status << std::endl;
        return false;
    }

    std::cout << "Waiting for XNET IP stack \"" << name << "\" to be ready..." << std::endl;
    nxIpStackWaitForInterface(stack, interface, 30000);
    return true;
}

void on_availability([[maybe_unused]] vsomeip::service_t _service, [[maybe_unused]] vsomeip::instance_t _instance, bool _is_available) {
    if (_is_available) {
        // Request the event and subscribe to its eventgroup
        std::set<vsomeip::eventgroup_t> groups;
        groups.insert(SAMPLE_EVENTGROUP_ID);
        client_app->request_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, groups, vsomeip::event_type_e::ET_FIELD);
        client_app->subscribe(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENTGROUP_ID);
    }
}

void on_message(const std::shared_ptr<vsomeip::message>& _response) {
    // Extract the payload from the received message
    std::uint32_t data;
    std::shared_ptr<vsomeip::payload> payload = _response->get_payload();
    std::memcpy(&data, payload->get_data(), sizeof(data));

    std::cout << "[client] Received: " << data << std::endl;
}

std::shared_ptr<vsomeip::application> create_app(const char* name, nxIpStackRef_t stack) {
    // Create a vsomeip application
    auto app = vsomeip::runtime::get()->create_application(name);

    // Initialize the application on its XNET IP stack
    if (!app->init(stack)) {
        std::cerr << "Couldn't initialize application \"" << name << "\"" << std::endl;
        return nullptr;
    }
    return app;
}

bool setup_server() {
    nxIpStackRef_t server_stack = nullptr;
    if (!create_xnet_stack(server_app_name, server::xnet_ip_stack_config, server::xnet_interface_name, server_stack)) {
        return false;
    }

    server_app = create_app(server_app_name, server_stack);
    if (!server_app) {
        return false;
    }

    // Offer the event and service
    std::set<vsomeip::eventgroup_t> its_groups;
    its_groups.insert(SAMPLE_EVENTGROUP_ID);
    server_app->offer_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, its_groups);
    server_app->offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
    return true;
}

bool setup_client() {
    nxIpStackRef_t client_stack = nullptr;
    if (!create_xnet_stack(client_app_name, client::xnet_ip_stack_config, client::xnet_interface_name, client_stack)) {
        return false;
    }

    client_app = create_app(client_app_name, client_stack);
    if (!client_app) {
        return false;
    }

    // Register a callback handlers for service availability and message reception
    client_app->register_availability_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, on_availability);
    client_app->register_message_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, on_message);

    // Request the service
    client_app->request_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
    return true;
}

void notify() {
    std::uint32_t counter = 0;

    // Create a payload for the notification
    std::shared_ptr<vsomeip::payload> payload = vsomeip::runtime::get()->create_payload();

    while (running) {
        // Initialize the payload with the counter value
        payload->set_data(reinterpret_cast<const vsomeip::byte_t*>(&counter), sizeof(counter));

        // Send the notification
        server_app->notify(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID, payload);

        std::cout << "[server] Notifying: " << counter++ << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main() {
    // Use the XNET socket factory for vsomeip
    vsomeip::use_xnet_socket_factory();

    // Setup the vsomeip applications
    if (!setup_server() || !setup_client()) {
        return 1;
    }

    // Register signal handler for clean shutdown
    std::signal(SIGINT, stop_application);
    std::signal(SIGTERM, stop_application);

    // start client application on its own thread
    client_thread = std::thread([] { client_app->start(); });

    // Start sending cyclic notifications
    notify_thread = std::thread(notify);

    // Start the server application
    server_app->start();

    if (client_thread.joinable()) {
        client_thread.join();
    }

    return 0;
}
