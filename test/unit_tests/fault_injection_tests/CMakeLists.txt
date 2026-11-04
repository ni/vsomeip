# --- NI modification: BEGIN ---
project("unit_tests_fault_injection_tests" LANGUAGES CXX)

add_executable(
    ${PROJECT_NAME}
    ../main.cpp
    test_fault_injection_manager.cpp
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
