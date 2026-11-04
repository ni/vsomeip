// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <iomanip>

#include <vsomeip/internal/logger.hpp>
#include "../../../../include/e2e/profile/profile04/protector.hpp"
#include "../../../../../utility/include/bithelper.hpp"
#include "../../../../../fault_injection/include/fault_injection_interface.hpp" // NI modification

namespace vsomeip_v3 {
namespace e2e {
namespace profile04 {

/** @req [SWS_E2E_00195] */
void protector::protect(e2e_buffer& _buffer, instance_t _instance) {
    std::scoped_lock lock(protect_mutex_);

    if (_instance > VSOMEIP_E2E_PROFILE04_MAX_INSTANCE) {
        VSOMEIP_ERROR << "E2E Profile 4 can only be used for instances [1-255]";
        return;
    }

    /** @req: [SWS_E2E_00363] */
    if (verify_inputs(_buffer)) {

        /** @req [SWS_E2E_00364] */
        bithelper::write_uint16_be(static_cast<uint16_t>(_buffer.size()), &_buffer[config_.offset_]);

        /** @req [SWS_E2E_00365] */
        bithelper::write_uint16_be(get_counter(_instance), &_buffer[config_.offset_ + 2]);

        /** @req [SWS_E2E_00366] */
        uint32_t its_data_id(config_.data_id_); // NI modification: instance id is not part of the data id
        bithelper::write_uint32_be(its_data_id, &_buffer[config_.offset_ + 4]);

        /** @req [SWS_E2E_00367] */
        uint32_t its_crc = profile_04::compute_crc(config_, _buffer);

        /** @req [SWS_E2E_0368] */
        bithelper::write_uint32_be(its_crc, &_buffer[config_.offset_ + 8]);

        /** @req [SWS_E2E_00369] */
        increment_counter(_instance);
    }
}

bool protector::verify_inputs(e2e_buffer& _buffer) {

    return (_buffer.size() >= config_.min_data_length_ && _buffer.size() <= config_.max_data_length_);
}

uint16_t protector::get_counter(instance_t _instance) const {

    uint16_t its_counter(0);

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

    // Check what type of faults to inject
    bool inject_counter = _fault_interface->has_fault(_id.first, _instance, _id.second, vsomeip_v3::fault_injection::FAULT_E2E_COUNTER);
    bool inject_crc = _fault_interface->has_fault(_id.first, _instance, _id.second, vsomeip_v3::fault_injection::FAULT_E2E_CRC);

    if (!inject_counter && !inject_crc) {
        return; // No faults to inject
    }

    if (inject_counter) {
        VSOMEIP_DEBUG << "profile04::protector: Injecting counter fault for service: 0x" << std::hex << _id.first << ", instance: 0x"
                      << _instance << ", event: 0x" << _id.second;

        // Profile 04 counter corruption
        // Counter is at offset + 2 (after length field), takes 2 bytes
        size_t counter_offset = config_.offset_ + 2;
        if (counter_offset + 1 < _buffer.size()) {
            uint16_t original_counter = bithelper::read_uint16_be(&_buffer[counter_offset]);
            _buffer[counter_offset] = 0xF0;
            _buffer[counter_offset + 1] = 0x0D;
            VSOMEIP_DEBUG << "profile04::protector: Corrupted counter at offset " << counter_offset << ", changed from 0x" << std::hex
                          << original_counter << " to 0xF00D";
        }
    }

    if (inject_crc) {
        VSOMEIP_DEBUG << "profile04::protector: Injecting CRC fault for service: 0x" << std::hex << _id.first << ", instance: 0x"
                      << _instance << ", event: 0x" << _id.second;

        // Profile 04 CRC corruption
        // CRC is at offset + 8 (after length + counter + data_id), takes 4 bytes
        size_t crc_offset = config_.offset_ + 8;
        if (crc_offset + 3 < _buffer.size()) {
            VSOMEIP_DEBUG << "profile04::protector: Corrupting CRC (4 bytes) at offset " << crc_offset << " to "
                          << (_buffer[crc_offset] == 0xDE ? "0x0BADBEEF" : "0xDEADBEEF");
            _buffer[crc_offset] = _buffer[crc_offset] == 0xDE ? 0x0B : 0xDE;
            _buffer[crc_offset + 1] = 0xAD;
            _buffer[crc_offset + 2] = 0xBE;
            _buffer[crc_offset + 3] = 0xEF;
        }
    }
}
// --- NI modification: END ---

} // namespace profile04
} // namespace e2e
} // namespace vsomeip_v3
