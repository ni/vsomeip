// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <string>

#if defined(__linux__) || defined(__QNX__)
#include <sys/socket.h>
#endif

#if defined(__linux__)
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

namespace vsomeip_v3::socket_option_helpers {

#if defined(__linux__) || defined(__QNX__)
inline bool set_bind_to_device(int _fd, std::string const& _device) {
    return ::setsockopt(_fd, SOL_SOCKET, SO_BINDTODEVICE, _device.c_str(), static_cast<socklen_t>(_device.size())) != -1;
}
#endif

#if defined(__linux__)
inline bool set_tcp_user_timeout(int _fd, unsigned int _timeout) {
    return ::setsockopt(_fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &_timeout, sizeof(_timeout)) != -1;
}

inline bool set_tcp_keepidle(int _fd, uint32_t _idle) {
    const auto its_opt = static_cast<int>(_idle);
    return ::setsockopt(_fd, IPPROTO_TCP, TCP_KEEPIDLE, &its_opt, sizeof(its_opt)) != -1;
}

inline bool set_tcp_keepintvl(int _fd, uint32_t _interval) {
    const auto its_opt = static_cast<int>(_interval);
    return ::setsockopt(_fd, IPPROTO_TCP, TCP_KEEPINTVL, &its_opt, sizeof(its_opt)) != -1;
}

inline bool set_tcp_keepcnt(int _fd, uint32_t _count) {
    const auto its_opt = static_cast<int>(_count);
    return ::setsockopt(_fd, IPPROTO_TCP, TCP_KEEPCNT, &its_opt, sizeof(its_opt)) != -1;
}

inline bool set_tcp_quick_ack(int _fd) {
    int its_flag = 1;
    return ::setsockopt(_fd, IPPROTO_TCP, TCP_QUICKACK, &its_flag, sizeof(its_flag)) != -1;
}

inline bool set_tcp_acceptor_reuse_port(int _fd) {
    int its_flag = 1;
    return ::setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &its_flag, sizeof(its_flag)) != -1;
}

inline bool set_tcp_acceptor_free_bind(int _fd) {
    int its_opt = 1;
    return ::setsockopt(_fd, IPPROTO_IP, IP_FREEBIND, &its_opt, sizeof(its_opt)) == 0;
}
#endif

} // namespace vsomeip_v3::socket_option_helpers
