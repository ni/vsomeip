// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <vsomeip/internal/logger.hpp>
#include "../../../../include/e2e/profile/profile_custom/protector.hpp"
#include "../../../../../fault_injection/include/fault_injection_interface.hpp" // NI modification

namespace vsomeip_v3 {
namespace e2e {
namespace profile_custom {

void protector::protect(e2e_buffer& _buffer, instance_t _instance) {

    (void)_instance;

    std::scoped_lock lock(protect_mutex_);

    if (profile_custom::is_buffer_length_valid(config_, _buffer)) {
        // compute the CRC over DataID and Data
        uint32_t computed_crc = profile_custom::compute_crc(config_, _buffer);
        // write CRC in Data
        write_crc(_buffer, computed_crc);
    }
}

void protector::write_crc(e2e_buffer& _buffer, uint32_t _computed_crc) {
    _buffer[config_.crc_offset_] = static_cast<uint8_t>(_computed_crc >> 24U);
    _buffer[config_.crc_offset_ + 1U] = static_cast<uint8_t>(_computed_crc >> 16U);
    _buffer[config_.crc_offset_ + 2U] = static_cast<uint8_t>(_computed_crc >> 8U);
    _buffer[config_.crc_offset_ + 3U] = static_cast<uint8_t>(_computed_crc);
}

// --- NI modification: BEGIN ---
void protector::inject_faults(e2e_buffer& _buffer, instance_t _instance, e2exf::data_identifier_t _id,
                              const fault_injection::fault_injection_interface* _fault_interface) {
    if (!_fault_interface || _buffer.empty()) {
        return;
    }

    // Check what type of faults to inject
    bool inject_crc = _fault_interface->has_fault(_id.first, _instance, _id.second, vsomeip_v3::fault_injection::FAULT_E2E_CRC);

    if (!inject_crc) {
        return; // No faults to inject
    }

    if (inject_crc) {
        VSOMEIP_DEBUG << "profile_custom::protector: Injecting CRC fault for service: 0x" << std::hex << _id.first << ", instance: 0x"
                      << _instance << ", event: 0x" << _id.second;

        // Profile Custom CRC corruption
        // CRC is at crc_offset, takes 4 bytes
        size_t crc_offset = config_.crc_offset_;
        if (crc_offset + 3 < _buffer.size()) {
            VSOMEIP_DEBUG << "profile_custom::protector: Corrupting CRC (4 bytes) at offset " << crc_offset << " to 0xCCCCCCCC";
            _buffer[crc_offset] = 0xCC;
            _buffer[crc_offset + 1] = 0xCC;
            _buffer[crc_offset + 2] = 0xCC;
            _buffer[crc_offset + 3] = 0xCC;
        }
    }

    // Note: Profile Custom does not have a counter field, so FAULT_E2E_COUNTER is not supported
}
// --- NI modification: END ---

} // namespace profile_custom
} // namespace e2e
} // namespace vsomeip_v3
