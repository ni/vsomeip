// Copyright (C) 2024 - XNET Integration
// Backend-neutral type definitions for XNET socket wrappers.
// When VSOMEIP_ENABLE_XNET is defined, maps to real NI XNET types.
// When not defined, provides minimal stubs so headers compile without NI SDK.

#ifndef XNET_TYPES_HPP_
#define XNET_TYPES_HPP_

#if defined(VSOMEIP_ENABLE_XNET)
#include "nxsocket.h"
#else

// Stub types for non-XNET builds (headers only, never instantiated on this path)
using nxSOCKET = int;
using nxIpStackRef_t = void*;
constexpr nxSOCKET nxINVALID_SOCKET = -1;

#endif // VSOMEIP_ENABLE_XNET

#endif // XNET_TYPES_HPP_
