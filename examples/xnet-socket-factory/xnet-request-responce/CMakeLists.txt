cmake_minimum_required(VERSION 3.13)

project(xnet-request-responce-tcp)

set(CMAKE_CXX_STANDARD 20)

set(VSOMEIP_NAME "vsomeip3")

find_package(${VSOMEIP_NAME} REQUIRED)


include_directories (${VSOMEIP_INCLUDE_DIRS})

set(XNET_TCP_SAMPLES
    xnet-request-tcp-client
    xnet-response-tcp-service
)

foreach(sample ${XNET_TCP_SAMPLES})
    add_executable(${sample} ${sample}.cpp)
    target_link_libraries(${sample} PRIVATE ${VSOMEIP_NAME})
endforeach()