// Copyright (C) 2024 - XNET Integration

#pragma once

#include <boost/system/error_code.hpp>

namespace vsomeip_v3 {

int xnet_get_last_error();
boost::system::error_code xnet_to_boost_error(int _xnet_error);

} // namespace vsomeip_v3
