#include "common/test_main.hpp"

#include "recording_socket_factory.hpp"

namespace {
std::shared_ptr<recording_socket_factory> factory = std::make_shared<recording_socket_factory>();
}

recording_socket_factory& get_recording_socket_factory() {
    return *factory;
}

int main(int argc, char** argv) {
    // Must happen before the first abstract_socket_factory::get() call.
    vsomeip_v3::set_abstract_factory(factory);
    return test_main(argc, argv);
}
