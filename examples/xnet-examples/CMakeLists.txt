add_executable(xnet-example xnet-example.cpp)
target_include_directories(xnet-example PRIVATE ${CMAKE_SOURCE_DIR}/implementation/endpoints/include)
target_link_libraries(xnet-example PRIVATE ${VSOMEIP_NAME} ${NIXNET_LIBRARY})
add_dependencies(xnet-example ${VSOMEIP_NAME})
add_custom_command(TARGET xnet-example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/config.json" "$<TARGET_FILE_DIR:xnet-example>/vsomeip/config.json"
    )
