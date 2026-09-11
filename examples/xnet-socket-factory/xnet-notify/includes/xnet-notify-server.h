#pragma once

#define SAMPLE_SERVICE_ID       0x1234
#define SAMPLE_INSTANCE_ID      0x5678
#define SAMPLE_EVENTGROUP_ID    0x4465
#define SAMPLE_EVENT_ID         0x8778

const char* xnet_ip_stack_config = R"(
{
    "schema":  "file:///NIXNET_Documentation/xnetIpStackSchema-07.json",
    "xnetInterfaces":  [
                           {
                               "name":  "ENET1",
                               "loopbackMode":  "externalAndInternal",
                               "MACs":  [
                                            {
                                                "address":  "generated",
                                                "VLANs":  [
                                                              {
                                                                  "IPv4":  {
                                                                               "mode":  "static",
                                                                               "staticAddresses":  [
                                                                                                       {
                                                                                                           "address":  "10.0.0.2",
                                                                                                           "subnetMask":  "255.255.255.0"
                                                                                                       }
                                                                                                   ]
                                                                           }
                                                              }
                                                          ]
                                            }
                                        ]
                           }
                       ]
}
)";