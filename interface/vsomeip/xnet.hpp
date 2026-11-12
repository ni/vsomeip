#pragma once

#include <vsomeip/export.hpp>

namespace vsomeip_v3 {

/// Installs the NI-XNET socket factory.
/// Must be called before the first vsomeip application is created.
/// \return True if the XNET socket factory is active, false if another
/// socket factory was already initialized.
VSOMEIP_IMPORT_EXPORT bool use_xnet_socket_factory();

}