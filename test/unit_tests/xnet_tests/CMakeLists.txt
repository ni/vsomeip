project("unit_tests_xnet_tests" LANGUAGES CXX)

set(SRCS 
    ../main.cpp
    test_xnet_socket_factory.cpp
    ut_xnet_api_seam.cpp
    ut_xnet_error_mapping.cpp
    ut_xnet_socket_factory_policy.cpp
    ut_xnet_tcp_acceptor.cpp
    ut_xnet_tcp_server_accept_policy.cpp
    ut_xnet_tcp_socket_async.cpp
    ut_xnet_tcp_socket_sync.cpp
    ut_xnet_udp_socket_async.cpp
    ut_xnet_udp_socket_sync.cpp
)

add_executable(${PROJECT_NAME} ${SRCS})
target_include_directories(${PROJECT_NAME} PRIVATE "../../../implementation/endpoints/include")

target_link_libraries(
    ${PROJECT_NAME}
    gtest
    gmock
    ${VSOMEIP_NAME}-test
    ${VSOMEIP_NAME}-cfg-test
    ${Boost_LIBRARIES}
    vsomeip_utilities
)

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
add_dependencies(build_unit_tests ${PROJECT_NAME})
