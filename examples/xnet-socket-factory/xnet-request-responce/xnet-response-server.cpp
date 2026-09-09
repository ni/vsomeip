#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <vsomeip/vsomeip.hpp>

#define SAMPLE_SERVICE_ID   0x1234
#define SAMPLE_INSTANCE_ID  0x5678
#define SAMPLE_METHOD_ID    0x0421

std::shared_ptr<vsomeip::application> app;

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

    // stop the application
    app->stop_offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
    app->stop();
}

int main() {
    // create a vsomeip application
    app = vsomeip::runtime::get()->create_application("xnet-response-server");
    
    // initialize the application
    if (!app->init()) {
        std::cerr << "Couldn't initialize application" << std::endl;
        return 1;
    }

    // register a message handler callback for messages sent to the service
    app->register_message_handler(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_METHOD_ID, on_message);

    // start offering the service
    app->offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

    // Start the application and wait for incoming messages
    app->start();

    return (0);
}
