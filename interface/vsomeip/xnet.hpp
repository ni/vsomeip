#pragma once

#include <vsomeip/export.hpp>

namespace vsomeip_v3 {

/// Installs the NI-XNET socket factory.
/// Must be called before the first vsomeip application is created.
VSOMEIP_IMPORT_EXPORT void use_xnet_socket_factory();

}