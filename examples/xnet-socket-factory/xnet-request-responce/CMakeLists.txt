cmake_minimum_required(VERSION 3.13)

project(xnet-request-responce-tcp)

set(CMAKE_CXX_STANDARD 20)

set(VSOMEIP_NAME "vsomeip3")

find_package(${VSOMEIP_NAME} REQUIRED)


include_directories (${VSOMEIP_INCLUDE_DIRS})

option(XNET_USE_TCP "Use the TCP configuration instead of UDP" OFF)

if(XNET_USE_TCP)
    set(XNET_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/xnet-request-responce-tcp.json")
else()
    set(XNET_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/xnet-request-responce-udp.json")
endif()

set(XNET_SAMPLES
    xnet-request-client
    xnet-response-server
)

foreach(sample ${XNET_SAMPLES})
    add_executable(${sample} ${sample}.cpp)
    target_link_libraries(${sample} PRIVATE ${VSOMEIP_NAME})

    if(XNET_USE_TCP)
        target_compile_definitions(${sample} PRIVATE XNET_USE_TCP)
    endif()

    # make the selected configuration available next to the executable as config.json
    add_custom_command(TARGET ${sample} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${XNET_CONFIG_FILE}" "$<TARGET_FILE_DIR:${sample}>/vsomeip/config.json"
    )
endforeach()