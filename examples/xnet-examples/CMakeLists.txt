# Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

add_executable(xnet-example xnet-example.cpp)
target_include_directories(xnet-example PRIVATE ${CMAKE_SOURCE_DIR}/implementation/endpoints/include)
target_link_libraries(xnet-example PRIVATE ${VSOMEIP_NAME} ${NIXNET_LIBRARY})
add_dependencies(xnet-example ${VSOMEIP_NAME})
