project("unit_tests_xnet_factory_tests" LANGUAGES CXX)

add_executable(${PROJECT_NAME}
    ../main.cpp
    ut_use_xnet_socket_factory.cpp
    ut_test_xnet_socket_factory.cpp
    ut_xnet_socket_factory_ip_stacks.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE "../../../implementation/endpoints/include")
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
