#ifndef VSOMEIP_V3_XNET_HPP_
#define VSOMEIP_V3_XNET_HPP_

#include "nxsocket.h" // Part of the public API: provides nxIpStackRef_t and the nx* stack functions.

namespace vsomeip_v3 {

/// Installs the NI-XNET socket factory using the given IP stack.
/// Must be called before the first vsomeip application is created.
void use_xnet_socket_factory(nxIpStackRef_t _xnet_stack);

} // namespace vsomeip_v3

#endif // VSOMEIP_V3_XNET_HPP_
