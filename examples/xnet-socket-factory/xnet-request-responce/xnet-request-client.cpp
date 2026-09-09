#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <vsomeip/vsomeip.hpp>

#define SAMPLE_SERVICE_ID   0x1234
#define SAMPLE_INSTANCE_ID  0x5678
#define SAMPLE_METHOD_ID    0x0421

std::shared_ptr<vsomeip::application> app;

void stop_application(int signum) {
    std::cout << "\nShutting down application..." << std::endl;
    if (app) {
        app->release_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
        app->stop();
    }
    std::exit(signum);
}

void on_availability(vsomeip::service_t _service, vsomeip::instance_t _instance, bool _is_available) {
    if (_service == SAMPLE_SERVICE_ID && _instance == SAMPLE_INSTANCE_ID && _is_available) {
        // send a request to the service
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

    std::string received_text(reinterpret_cast<const char*>(response_payload->get_data()),
                              response_payload->get_length());

    std::cout << "Received from " << std::setw(4) << std::setfill('0') << std::hex
              << _response->get_client() << ": " << received_text << std::endl;

    // stop the application
    app->release_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
    app->stop();
}

int main() {
    // create a vsomeip application
    app = vsomeip::runtime::get()->create_application("xnet-request-client");

    // initialize the application
    if (!app->init()) {
        std::cerr << "Couldn't initialize application" << std::endl;
        return 1;
    }

    // register a callback for responses from the service
    app->register_message_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_METHOD_ID, on_message);

    // register a callback which is called as soon as the service is available
    app->register_availability_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, on_availability);

    // request the service
    app->request_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

    // start the application
    app->start();
}
