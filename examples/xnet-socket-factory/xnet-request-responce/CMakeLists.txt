cmake_minimum_required(VERSION 3.13)

project(xnet-request-responce-tcp)

set(CMAKE_CXX_STANDARD 20)

set(VSOMEIP_NAME "vsomeip3")

find_package(${VSOMEIP_NAME} REQUIRED)

# vsomeip headers include Boost.Asio, and the exported vsomeip3 target links
# Boost PRIVATE, so its include directories must be added explicitly here.
find_package(Boost 1.75 COMPONENTS filesystem REQUIRED)

include_directories (${VSOMEIP_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/../../../implementation/endpoints/include)
include_directories (SYSTEM ${Boost_INCLUDE_DIRS})

option(USE_TCP "Use the TCP configuration instead of UDP" OFF)

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

if(USE_TCP)
    set(CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/xnet-request-responce-tcp.json")
else()
    set(CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/xnet-request-responce-udp.json")
endif()

set(SAMPLES
    xnet-request-client
    xnet-response-server
)

foreach(sample ${SAMPLES})
    add_executable(${sample} ${sample}.cpp)
    target_link_libraries(${sample} PRIVATE ${VSOMEIP_NAME} ${NIXNET_LIBRARY})
endforeach()

# make the selected configuration available next to the executables (done once)
list(GET SAMPLES 0 FIRST_SAMPLE)
add_custom_command(TARGET ${FIRST_SAMPLE} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${FIRST_SAMPLE}>/vsomeip"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CONFIG_FILE}" "$<TARGET_FILE_DIR:${FIRST_SAMPLE}>/vsomeip/"
    COMMENT "Copying XNET configuration next to the executables"
)