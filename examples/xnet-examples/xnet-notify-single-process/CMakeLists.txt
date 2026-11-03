cmake_minimum_required(VERSION 3.13)

project(xnet-notify-single-process)

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
# When built as part of the vsomeip tree, NIXNET_INCLUDE_DIR/NIXNET_LIBRARY are
# already resolved by the top-level CMakeLists.txt. Only detect them here when
# this example is configured standalone.
if(NOT NIXNET_LIBRARY)
    if(DEFINED ENV{NIEXTCCOMPILERSUPP} AND NOT "$ENV{NIEXTCCOMPILERSUPP}" STREQUAL "")
        set(NIXNET_INCLUDE_DIR "$ENV{NIEXTCCOMPILERSUPP}/include")
        if(MSVC)
            set(NIXNET_LIBRARY_DIR "$ENV{NIEXTCCOMPILERSUPP}/lib64/msvc")
        else()
            set(NIXNET_LIBRARY_DIR "$ENV{NIEXTCCOMPILERSUPP}/lib64")
        endif()
    elseif(MSVC)
        message(FATAL_ERROR "NIEXTCCOMPILERSUPP is not set. Cannot locate nxsocket.h/nixntipstack.")
    endif()

    if(MSVC)
        set(NIXNET_LIBRARY "${NIXNET_LIBRARY_DIR}/nixntipstack.lib")
    else()
        find_library(NIXNET_LIBRARY nixntipstack HINTS "${NIXNET_LIBRARY_DIR}")
        if(NOT NIXNET_LIBRARY)
            message(FATAL_ERROR "nixntipstack library was not found.")
        endif()
    endif()
endif()

if(NIXNET_INCLUDE_DIR)
    include_directories (SYSTEM ${NIXNET_INCLUDE_DIR})
endif()

# The configuration is provided in-memory, no JSON files are deployed.
set(sample xnet-notify-single-process)
set(INSTALL_DIR "examples/xnet-examples/${PROJECT_NAME}")

add_executable(${sample} ${CMAKE_CURRENT_SOURCE_DIR}/${sample}.cpp)
target_link_libraries(${sample} PRIVATE ${VSOMEIP_NAME} ${NIXNET_LIBRARY})
set_target_properties(${sample} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_BINARY_DIR}/${sample}>"
)

install(
    TARGETS ${sample}
    RUNTIME DESTINATION "${INSTALL_DIR}/${sample}" COMPONENT bin
)
