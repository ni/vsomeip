# --- NI modification: BEGIN ---
project("unit_tests_e2e_tests" LANGUAGES CXX)

set(E2E_SRC_DIR ${CMAKE_SOURCE_DIR}/implementation/e2e_protection/src)

add_executable(
    ${PROJECT_NAME}
    ../main.cpp
    test_profile05_fault_injection.cpp
    ${E2E_SRC_DIR}/buffer/buffer.cpp
    ${E2E_SRC_DIR}/crc/crc.cpp
    ${E2E_SRC_DIR}/e2e/profile/profile05/checker.cpp
    ${E2E_SRC_DIR}/e2e/profile/profile05/profile_05.cpp
    ${E2E_SRC_DIR}/e2e/profile/profile05/protector.cpp
)

target_link_libraries(
    ${PROJECT_NAME}
    gtest
    ${VSOMEIP_NAME}-test
    vsomeip_utilities
)

add_dependencies(build_unit_tests ${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)
# --- NI modification: END ---
