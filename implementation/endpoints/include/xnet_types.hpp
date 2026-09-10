#ifndef XNET_TYPES_HPP_
#define XNET_TYPES_HPP_

#if defined(VSOMEIP_ENABLE_XNET)
#include "nxsocket.h"
#else

// Non-XNET builds map the nx* vocabulary onto the native socket API so that the
// backend-agnostic parts of the endpoint implementations compile unchanged.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

using nxIpStackRef_t = void*;

#if defined(_WIN32)
using nxSOCKET = SOCKET;
const nxSOCKET nxINVALID_SOCKET = INVALID_SOCKET;
using nxsocklen_t = int;
#else
using nxSOCKET = int;
constexpr nxSOCKET nxINVALID_SOCKET = -1;
using nxsocklen_t = socklen_t;
#endif

using nxsockaddr = sockaddr;
using nxsockaddr_storage = sockaddr_storage;
using nxsockaddr_in = sockaddr_in;
using nxsockaddr_in6 = sockaddr_in6;
using nxin_addr = in_addr;
using nxip_mreq = ip_mreq;
using nxipv6_mreq = ipv6_mreq;

constexpr int nxAF_INET = AF_INET;
constexpr int nxAF_INET6 = AF_INET6;
#if defined(_WIN32)
constexpr int nxSHUT_RD = SD_RECEIVE;
constexpr int nxSHUT_WR = SD_SEND;
constexpr int nxSHUT_RDWR = SD_BOTH;
#else
constexpr int nxSHUT_RD = SHUT_RD;
constexpr int nxSHUT_WR = SHUT_WR;
constexpr int nxSHUT_RDWR = SHUT_RDWR;
#endif

#endif // VSOMEIP_ENABLE_XNET

#endif // XNET_TYPES_HPP_
