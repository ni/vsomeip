// --- NI modification: BEGIN ---
#include <gtest/gtest.h>

#include "../../../implementation/e2e_protection/include/e2e/profile/profile05/checker.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile05/protector.hpp"
#include "../../../implementation/fault_injection/include/fault_injection_interface.hpp"

using namespace vsomeip_v3;
using e2e::profile_interface::generic_check_status;

namespace {

class fake_faults : public fault_injection::fault_injection_interface {
public:
    std::uint32_t faults_ = fault_injection::FAULT_NONE;
    std::uint32_t get_fault_types(service_t, instance_t, event_t) const override { return faults_; }
    bool is_disabled(service_t, instance_t, event_t) const override { return false; }
};

class profile05_fault_injection : public ::testing::Test {
protected:
    // Returns the checker status for the next protected (and possibly faulted) message.
    e2e::profile_interface::check_status_t send(std::uint32_t _faults) {
        e2e_buffer its_buffer(16, 0x42);
        protector_.protect(its_buffer, instance_);
        faults_.faults_ = _faults;
        protector_.inject_faults(its_buffer, instance_, {0x1234, 0x8001}, &faults_);
        e2e::profile_interface::check_status_t its_status;
        checker_.check(its_buffer, instance_, its_status);
        return its_status;
    }

    const instance_t instance_ = 1;
    e2e::profile05::profile_config config_{0x1234, 64, 0, 1};
    e2e::profile05::protector protector_{config_};
    e2e::profile05::profile_05_checker checker_{config_};
    fake_faults faults_;
};

} // namespace

TEST_F(profile05_fault_injection, no_fault_passes_check) {
    EXPECT_EQ(send(fault_injection::FAULT_NONE), generic_check_status::E2E_OK);
    EXPECT_EQ(send(fault_injection::FAULT_NONE), generic_check_status::E2E_OK);
}

TEST_F(profile05_fault_injection, counter_fault_keeps_crc_valid_and_fails_sequence) {
    EXPECT_EQ(send(fault_injection::FAULT_NONE), generic_check_status::E2E_OK);
    EXPECT_EQ(send(fault_injection::FAULT_E2E_COUNTER), generic_check_status::E2E_ERROR);
}

TEST_F(profile05_fault_injection, crc_fault_fails_crc) {
    EXPECT_EQ(send(fault_injection::FAULT_E2E_CRC), generic_check_status::E2E_WRONG_CRC);
    EXPECT_EQ(send(fault_injection::FAULT_E2E_COUNTER | fault_injection::FAULT_E2E_CRC), generic_check_status::E2E_WRONG_CRC);
}
// --- NI modification: END ---
