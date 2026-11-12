project("unit_tests_xnet_application_tests" LANGUAGES CXX)

set(SRCS
    main.cpp
    ut_xnet_application_ip_stack.cpp
)

add_executable(${PROJECT_NAME} ${SRCS})
target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
    "../../../implementation/endpoints/include"
    "../../../implementation/runtime/include"
)

target_link_libraries(
    ${PROJECT_NAME}
    gtest
    ${VSOMEIP_NAME}-test
    ${VSOMEIP_NAME}-cfg-test
    ${Boost_LIBRARIES}
    vsomeip_utilities
)

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
add_dependencies(build_unit_tests ${PROJECT_NAME})
