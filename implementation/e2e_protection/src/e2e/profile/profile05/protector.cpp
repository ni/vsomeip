// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <iomanip>

#include <vsomeip/internal/logger.hpp>
#include "../../../../include/e2e/profile/profile05/protector.hpp"
#include "../../../../../utility/include/bithelper.hpp"
#include "../../../../../fault_injection/include/fault_injection_interface.hpp" // NI modification

namespace vsomeip_v3 {
namespace e2e {
namespace profile05 {

void protector::protect(e2e_buffer& _buffer, instance_t _instance) {

    (void)_instance;

    std::scoped_lock lock(protect_mutex_);

    if (_instance > VSOMEIP_E2E_PROFILE05_MAX_INSTANCE) {
        VSOMEIP_ERROR << "E2E Profile 5 can only be used for instances [1-255]";
        return;
    }

    if (profile_05::is_buffer_length_valid(config_, _buffer)) {
        // write the current Counter value in Data
        write_counter(_buffer, get_counter(_instance), 2);

        // compute the CRC
        uint16_t its_crc = profile_05::compute_crc(config_, _buffer);
        bithelper::write_uint16_be(its_crc, &_buffer[config_.offset_]);

        // increment the Counter (new value will be used in the next invocation of
        // E2E_P05Protect()),
        increment_counter(_instance);
    }
}

void protector::write_counter(e2e_buffer& _buffer, uint8_t _data, size_t _index) {

    _buffer[config_.offset_ + _index] = _data;
}

uint8_t protector::get_counter(instance_t _instance) const {

    uint8_t its_counter(0);

    auto find_counter = counter_.find(_instance);
    if (find_counter != counter_.end()) {
        its_counter = find_counter->second;
    }

    return its_counter;
}

void protector::increment_counter(instance_t _instance) {

    auto find_counter = counter_.find(_instance);
    if (find_counter != counter_.end()) {
        find_counter->second++;
    } else {
        counter_[_instance] = 1;
    }
}

// --- NI modification: BEGIN ---
void protector::inject_faults(e2e_buffer& _buffer, instance_t _instance, e2exf::data_identifier_t _id,
                              const fault_injection::fault_injection_interface* _fault_interface) {
    if (!_fault_interface || _buffer.empty()) {
        return;
    }

    bool inject_counter = _fault_interface->has_fault(_id.first, _instance, _id.second, vsomeip_v3::fault_injection::FAULT_E2E_COUNTER);
    bool inject_crc = _fault_interface->has_fault(_id.first, _instance, _id.second, vsomeip_v3::fault_injection::FAULT_E2E_CRC);

    if (!inject_counter && !inject_crc) {
        return;
    }

    // Profile 05 header: CRC16 at offset (2 bytes), counter at offset + 2 (1 byte)
    if (inject_counter) {
        VSOMEIP_DEBUG << "profile05::protector: Injecting counter fault for service: 0x" << std::hex << _id.first << ", instance: 0x"
                      << _instance << ", event: 0x" << _id.second;

        size_t counter_offset = config_.offset_ + 2;
        if (counter_offset < _buffer.size()) {
            uint8_t original_counter = _buffer[counter_offset];
            // A fixed value would equal the real 8-bit counter once every 256 messages; flipping the MSB always jumps by 128.
            _buffer[counter_offset] = static_cast<uint8_t>(original_counter ^ 0x80);
            VSOMEIP_DEBUG << "profile05::protector: Corrupted counter at offset " << counter_offset << ", changed from 0x" << std::hex
                          << static_cast<int>(original_counter) << " to 0x" << static_cast<int>(_buffer[counter_offset]);

            if (!inject_crc) {
                // Recompute CRC with faulted counter so the receiver detects the sequence error, not a CRC error
                uint16_t its_crc = profile_05::compute_crc(config_, _buffer);
                bithelper::write_uint16_be(its_crc, &_buffer[config_.offset_]);
            }
        }
    }

    if (inject_crc) {
        VSOMEIP_DEBUG << "profile05::protector: Injecting CRC fault for service: 0x" << std::hex << _id.first << ", instance: 0x"
                      << _instance << ", event: 0x" << _id.second;

        size_t crc_offset = config_.offset_;
        if (crc_offset + 1 < _buffer.size()) {
            VSOMEIP_DEBUG << "profile05::protector: Corrupting CRC (2 bytes) at offset " << crc_offset << " to "
                          << (_buffer[crc_offset] == 0xDE ? "0x0BAD" : "0xDEAD");
            _buffer[crc_offset] = _buffer[crc_offset] == 0xDE ? 0x0B : 0xDE;
            _buffer[crc_offset + 1] = 0xAD;
        }
    }
}
// --- NI modification: END ---

} // namespace profile05
} // namespace e2e
} // namespace vsomeip_v3
