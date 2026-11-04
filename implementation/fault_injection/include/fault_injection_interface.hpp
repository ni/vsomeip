// Copyright (C) 2025 NI
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_FAULT_INJECTION_INTERFACE_HPP
#define VSOMEIP_V3_FAULT_INJECTION_INTERFACE_HPP

#include <cstdint>
#include <vsomeip/primitive_types.hpp>

#include "../../../interface/vsomeip/export.hpp"

namespace vsomeip_v3 {
namespace fault_injection {

/**
 * @brief Fault injection types as bit field
 */
enum fault_type_e : std::uint32_t {
    FAULT_NONE = 0x00,
    FAULT_E2E_COUNTER = 0x01, // Corrupt E2E counter
    FAULT_E2E_CRC = 0x02, // Corrupt E2E CRC
    FAULT_ALL = 0xFF // All available faults
};

/**
 * @brief Fault injection state structure
 *
 * Contains the disabled state and fault types for a fault injection rule.
 */
struct fault_state_t {
    bool disabled_; // Whether the fault injection is disabled
    fault_type_e faults_; // Fault types to inject

    fault_state_t() : disabled_(false), faults_(FAULT_NONE) { }
    fault_state_t(bool _disabled, fault_type_e _faults) : disabled_(_disabled), faults_(_faults) { }
};

/**
 * @brief Simple interface for fault injection queries
 *
 * Provides a clean interface for E2E modules to query fault injection settings.
 */
class fault_injection_interface {
public:
    virtual ~fault_injection_interface() = default;

    /**
     * @brief Get fault types for given service/instance/method combination
     * @param _service Service ID
     * @param _instance Instance ID
     * @param _method_event Method or Event ID
     * @return Bit field of fault_type_e values, or FAULT_NONE if no faults
     */
    virtual std::uint32_t get_fault_types(service_t _service, instance_t _instance, event_t _method_event) const = 0;

    /**
     * @brief Check if fault injection is disabled for given service/instance/method combination
     * @param _service Service ID
     * @param _instance Instance ID
     * @param _method_event Method or Event ID
     * @return true if disabled, false otherwise
     */
    virtual bool is_disabled(service_t _service, instance_t _instance, event_t _method_event) const = 0;

    /**
     * @brief Convenience method to check for specific fault type
     */
    bool has_fault(service_t _service, instance_t _instance, event_t _method_event, fault_type_e _fault_type) const {
        return (get_fault_types(_service, _instance, _method_event) & _fault_type) != 0;
    }
};

} // namespace fault_injection
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_FAULT_INJECTION_INTERFACE_HPP