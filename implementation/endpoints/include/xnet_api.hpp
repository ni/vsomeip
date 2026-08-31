#ifndef XNET_API_HPP_
#define XNET_API_HPP_

#include "xnet_types.hpp"

#if defined(VSOMEIP_ENABLE_XNET)

namespace vsomeip_v3::xnet_api {

struct api_table {
    decltype(&::nxgetlasterrornum) nxgetlasterrornum_fn{&::nxgetlasterrornum};
    decltype(&::nxsocket) nxsocket_fn{&::nxsocket};
    decltype(&::nxbind) nxbind_fn{&::nxbind};
    decltype(&::nxclose) nxclose_fn{&::nxclose};
    decltype(&::nxshutdown) nxshutdown_fn{&::nxshutdown};
    decltype(&::nxsetsockopt) nxsetsockopt_fn{&::nxsetsockopt};
    decltype(&::nxgetsockopt) nxgetsockopt_fn{&::nxgetsockopt};
    decltype(&::nxgetsockname) nxgetsockname_fn{&::nxgetsockname};
    decltype(&::nxconnect) nxconnect_fn{&::nxconnect};
    decltype(&::nxrecvfrom) nxrecvfrom_fn{&::nxrecvfrom};
    decltype(&::nxsend) nxsend_fn{&::nxsend};
    decltype(&::nxsendto) nxsendto_fn{&::nxsendto};
    decltype(&::nxrecv) nxrecv_fn{&::nxrecv};
    decltype(&::nxaccept) nxaccept_fn{&::nxaccept};
    decltype(&::nxlisten) nxlisten_fn{&::nxlisten};
    decltype(&::nxselect) nxselect_fn{&::nxselect};
};

// Returns an immutable snapshot of the active api_table. The snapshot is taken
// atomically, so it is safe to call concurrently with set_api_table_for_test /
// reset_api_table_for_test (which atomically swap in a new table).
api_table get_api_table();
void set_api_table_for_test(const api_table& _table);
void reset_api_table_for_test();

inline auto nxgetlasterrornum() {
    return get_api_table().nxgetlasterrornum_fn();
}

inline nxSOCKET nxsocket(nxIpStackRef_t _stack, int _af, int _type, int _proto) {
    return get_api_table().nxsocket_fn(_stack, _af, _type, _proto);
}

inline int nxbind(nxSOCKET _socket, nxsockaddr* _addr, nxsocklen_t _len) {
    return get_api_table().nxbind_fn(_socket, _addr, _len);
}

inline int nxclose(nxSOCKET _socket) {
    return get_api_table().nxclose_fn(_socket);
}

inline int nxshutdown(nxSOCKET _socket, int _how) {
    return get_api_table().nxshutdown_fn(_socket, _how);
}

inline int nxsetsockopt(nxSOCKET _socket, int _level, int _name, const void* _value, nxsocklen_t _len) {
    return get_api_table().nxsetsockopt_fn(_socket, _level, _name, _value, _len);
}

inline int nxgetsockopt(nxSOCKET _socket, int _level, int _name, void* _value, nxsocklen_t* _len) {
    return get_api_table().nxgetsockopt_fn(_socket, _level, _name, _value, _len);
}

inline int nxgetsockname(nxSOCKET _socket, nxsockaddr* _addr, nxsocklen_t* _len) {
    return get_api_table().nxgetsockname_fn(_socket, _addr, _len);
}

inline int nxconnect(nxSOCKET _socket, nxsockaddr* _addr, nxsocklen_t _len) {
    return get_api_table().nxconnect_fn(_socket, _addr, _len);
}

inline int32_t nxrecvfrom(nxSOCKET _socket, void* _buffer, int32_t _len, int _flags, nxsockaddr* _addr, nxsocklen_t* _addr_len) {
    return get_api_table().nxrecvfrom_fn(_socket, _buffer, _len, _flags, _addr, _addr_len);
}

inline int32_t nxsend(nxSOCKET _socket, const void* _buffer, int32_t _len, int _flags) {
    return get_api_table().nxsend_fn(_socket, _buffer, _len, _flags);
}

inline int32_t nxsendto(nxSOCKET _socket, const void* _buffer, int32_t _len, int _flags, nxsockaddr* _addr, nxsocklen_t _addr_len) {
    return get_api_table().nxsendto_fn(_socket, _buffer, _len, _flags, _addr, _addr_len);
}

inline int32_t nxrecv(nxSOCKET _socket, void* _buffer, int32_t _len, int _flags) {
    return get_api_table().nxrecv_fn(_socket, _buffer, _len, _flags);
}

inline nxSOCKET nxaccept(nxSOCKET _socket, nxsockaddr* _addr, nxsocklen_t* _len) {
    return get_api_table().nxaccept_fn(_socket, _addr, _len);
}

inline int nxlisten(nxSOCKET _socket, int _backlog) {
    return get_api_table().nxlisten_fn(_socket, _backlog);
}

inline int nxselect(int _nfds, nxfd_set* _readfds, nxfd_set* _writefds, nxfd_set* _exceptfds, nxtimeval* _timeout) {
    return get_api_table().nxselect_fn(_nfds, _readfds, _writefds, _exceptfds, _timeout);
}

} // namespace vsomeip_v3::xnet_api

#endif // VSOMEIP_ENABLE_XNET

#endif // XNET_API_HPP_
