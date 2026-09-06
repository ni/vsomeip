#pragma once

#include <string>

// Client id assigned to the sample application in the in-memory configuration.
// The test uses this value to prove the in-memory JSON string was actually
// parsed and applied (instead of falling back to a configuration file).
#define SAMPLE_CLIENT_ID 0x1212

// Complete vsomeip configuration encoded as an in-memory JSON string.
//
// This is passed directly to vsomeip::application::init(const std::string&),
// so vsomeip never reads a vsomeip.json file from disk and never writes a
// temporary file. The application is configured as its own routing host so the
// single-process sample can offer and request the same service.
inline const std::string kInMemoryVsomeipJson = R"json(
{
	"unicast": "127.0.0.1",
	"logging": {
		"level": "info",
		"console": "false",
		"dlt": "false"
	},
	"applications": [
		{
			"name": "JsonConfigApp",
			"id": "0x1212"
		}
	],
	"services": [
		{
			"service": "0x1234",
			"instance": "0x5678",
			"unreliable": "30509"
		}
	],
	"routing": "JsonConfigApp",
	"service-discovery": {
		"enable": "true",
		"multicast": "224.224.224.245",
		"port": "30490",
		"protocol": "udp",
		"initial_delay_min": "10",
		"initial_delay_max": "100",
		"repetitions_base_delay": "200",
		"repetitions_max": "3",
		"ttl": "3",
		"cyclic_offer_delay": "2000",
		"request_response_delay": "1500"
	}
}
)json";
