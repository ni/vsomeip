// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <vsomeip/primitive_types.hpp>

#include "../../../buffer/buffer.hpp"
#include "../profile_interface/profile_interface.hpp"
// --- NI modification: BEGIN ---
#include <utility>

namespace vsomeip_v3 {
namespace fault_injection {
class fault_injection_interface;
}
namespace e2exf {
using data_identifier_t = std::pair<service_t, event_t>; // same as in e2exf/config.hpp (which includes this file)
}
} // namespace vsomeip_v3
// --- NI modification: END ---

namespace vsomeip_v3 {
namespace e2e {
namespace profile_interface {

class protector : public profile_interface {
public:
    virtual void protect(e2e_buffer& _buffer, instance_t _instance) = 0;

    // --- NI modification: BEGIN ---
    /**
     * @brief Inject faults into E2E protected buffer based on fault injection interface
     * @param _buffer Buffer containing E2E data
     * @param _instance Instance ID for fault lookup
     * @param _id Data identifier for fault lookup
     * @param _fault_interface Fault injection interface to query for faults
     *
     * Default implementation does nothing. Profiles that support fault injection
     * should override this method.
     */
    virtual void inject_faults(e2e_buffer& _buffer, instance_t _instance, e2exf::data_identifier_t _id,
                               const fault_injection::fault_injection_interface* _fault_interface) {
        (void)_buffer;
        (void)_instance;
        (void)_id;
        (void)_fault_interface;
    }
    // --- NI modification: END ---
};

} // namespace profile_interface
} // namespace e2e
} // namespace vsomeip_v3
