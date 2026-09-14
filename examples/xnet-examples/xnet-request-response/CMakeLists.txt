cmake_minimum_required(VERSION 3.13)

project(xnet-request-response)

set(CMAKE_CXX_STANDARD 20)

set(VSOMEIP_NAME "vsomeip3")

if(NOT TARGET ${VSOMEIP_NAME})
	find_package(${VSOMEIP_NAME} REQUIRED)
	find_package(Boost COMPONENTS filesystem REQUIRED)
endif()

include_directories (
    ${VSOMEIP_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/includes
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../implementation/endpoints/include
)

include_directories (SYSTEM ${Boost_INCLUDE_DIRS})

# NI-XNET library    
if(NOT DEFINED ENV{NIEXTCCOMPILERSUPP})
    message(FATAL_ERROR "NIEXTCCOMPILERSUPP is not set. Cannot locate nxsocket.h/nixntipstack.")
endif()

set(NIXNET_INCLUDE_DIR "$ENV{NIEXTCCOMPILERSUPP}/include")
set(NIXNET_LIBRARY_DIR "$ENV{NIEXTCCOMPILERSUPP}/lib64/msvc")
set(NIXNET_LIBRARY "${NIXNET_LIBRARY_DIR}/nixntipstack.lib")

include_directories (${NIXNET_INCLUDE_DIR})

# Select the configurations to use (UDP by default, TCP when USE_TCP is ON)
option(USE_TCP "Use the TCP configuration instead of UDP" OFF)
if(USE_TCP)
    set(CONFIG_PREFIX "xnet-request-response-tcp")
else()
    set(CONFIG_PREFIX "xnet-request-response-udp")
endif()

set(CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/configurations")
set(xnet-request-response-client_CONFIG "${CONFIG_DIR}/xnet-request-response-client.json")
set(xnet-request-response-server_CONFIG "${CONFIG_DIR}/${CONFIG_PREFIX}-server.json")

set(SAMPLES
    xnet-request-response-client
    xnet-request-response-server
)

set(INSTALL_DIR "examples/xnet-examples/${PROJECT_NAME}")

foreach(sample ${SAMPLES})
    add_executable(${sample} ${CMAKE_CURRENT_SOURCE_DIR}/${sample}.cpp)
    target_link_libraries(${sample} PRIVATE ${VSOMEIP_NAME} ${NIXNET_LIBRARY})
    set_target_properties(${sample} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/${sample}>"
    )

    # make the matching configuration available next to the executable
    add_custom_command(TARGET ${sample} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
                "$<TARGET_FILE_DIR:${sample}>/vsomeip"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${sample}>/vsomeip"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${${sample}_CONFIG}" "$<TARGET_FILE_DIR:${sample}>/vsomeip/"
        COMMENT "Copying XNET configuration for ${sample}"
    )

    install(
        TARGETS ${sample}
        RUNTIME DESTINATION "${INSTALL_DIR}/${sample}" COMPONENT bin
    )
    install(
        FILES "${${sample}_CONFIG}"
        DESTINATION "${INSTALL_DIR}/${sample}/vsomeip" COMPONENT config
    )
endforeach()