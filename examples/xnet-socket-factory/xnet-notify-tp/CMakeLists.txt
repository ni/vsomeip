cmake_minimum_required(VERSION 3.13)

project(xnet-notify-tp)

set(CMAKE_CXX_STANDARD 20)

set(VSOMEIP_NAME "vsomeip3")

find_package(${VSOMEIP_NAME} REQUIRED)

# vsomeip headers include Boost.Asio, and the exported vsomeip3 target links
# Boost PRIVATE, so its include directories must be added explicitly here.
find_package(Boost COMPONENTS filesystem REQUIRED)

include_directories (${VSOMEIP_INCLUDE_DIRS}
                     ${CMAKE_CURRENT_SOURCE_DIR}/includes
                     ${CMAKE_CURRENT_SOURCE_DIR}/../../../implementation/endpoints/include)
include_directories (SYSTEM ${Boost_INCLUDE_DIRS})

################################################################################
# NI-XNET (nixntipstack) library    
################################################################################
if(NOT DEFINED ENV{NIEXTCCOMPILERSUPP})
    message(FATAL_ERROR "NIEXTCCOMPILERSUPP is not set. Cannot locate nxsocket.h/nixntipstack.")
endif()

set(NIXNET_INCLUDE_DIR "$ENV{NIEXTCCOMPILERSUPP}/include")
set(NIXNET_LIBRARY_DIR "$ENV{NIEXTCCOMPILERSUPP}/lib64/msvc")
set(NIXNET_LIBRARY "${NIXNET_LIBRARY_DIR}/nixntipstack.lib")

include_directories (${NIXNET_INCLUDE_DIR})

set(CONFIG_PREFIX "xnet-notify-tp-udp")

set(CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/configurations")

# sample name -> configuration suffix (client/server)
set(SAMPLES
    xnet-notify-tp-client
    xnet-notify-tp-server
)

set(xnet-notify-tp-client_CONFIG "${CONFIG_DIR}/${CONFIG_PREFIX}-client.json")
set(xnet-notify-tp-server_CONFIG "${CONFIG_DIR}/${CONFIG_PREFIX}-server.json")

foreach(sample ${SAMPLES})
    add_executable(${sample} ${CMAKE_CURRENT_SOURCE_DIR}/src/${sample}.cpp)
    target_link_libraries(${sample} PRIVATE ${VSOMEIP_NAME} ${NIXNET_LIBRARY})

    # make the matching configuration available next to the executable
    add_custom_command(TARGET ${sample} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${sample}>/vsomeip"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${${sample}_CONFIG}" "$<TARGET_FILE_DIR:${sample}>/vsomeip/"
        COMMENT "Copying XNET configuration for ${sample}"
    )
endforeach()