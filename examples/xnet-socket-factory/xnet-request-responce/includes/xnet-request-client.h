#pragma once

#define SAMPLE_SERVICE_ID   0x1234
#define SAMPLE_INSTANCE_ID  0x5678
#define SAMPLE_METHOD_ID    0x0421

const char* xnet_ip_stack_config = R"(
{
    "schema":  "file:///NIXNET_Documentation/xnetIpStackSchema-07.json",
    "xnetInterfaces":  [
                           {
                               "name":  "ENET2",
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
                                                                                                           "address":  "10.0.0.3",
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
