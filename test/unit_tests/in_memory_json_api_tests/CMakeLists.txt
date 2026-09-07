project("unit_tests_in_memory_json_api" LANGUAGES CXX)

file(GLOB SRCS ../main.cpp *.cpp)

set(THREADS_PREFER_PTHREAD_FLAG ON)

# ----------------------------------------------------------------------------
# Executable and libraries to link
# ----------------------------------------------------------------------------
add_executable(${PROJECT_NAME} ${SRCS})
target_link_libraries(
    ${PROJECT_NAME}
    vsomeip3-test
    vsomeip3-cfg-test
    ${Boost_LIBRARIES}
    gtest
    vsomeip_utilities
)

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

add_dependencies(build_unit_tests ${PROJECT_NAME})
