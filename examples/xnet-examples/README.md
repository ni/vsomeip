# vsomeip XNET examples

These examples show how to run vsomeip on top of the **NI-XNET IP stack** instead of the
standard operating system TCP/IP stack. Each example creates its own XNET IP stack
instance, installs the `vsomeip_v3::xnet_socket_factory` as the abstract socket factory and
then uses the regular vsomeip API.

Available examples:

| Example | Description |
|---|---|
| `xnet-notify` | Server offers a field/event, client subscribes and receives cyclic notifications. |
| `xnet-notify-tp` | Same as `xnet-notify`, but with a large payload that is segmented by SOME/IP-TP. |
| `xnet-request-responce` | Client sends a request, server answers with a response. |

## Hardware and software requirements

* An **NI-XNET Automotive Ethernet** interface with loopback connected two ports.
  One port is used by the server, the other by the client.
  By default, `ENET1` is used for the server and `ENET2` is used for the client. The interface names can be changed in [`xnet-stack-configuration.h`](common/includes/xnet-stack-configuration.h).
* **NI-XNET driver** installed, including the IP stack support (`nxsocket.h`, `nixnet.h`,
  `nixntipstack.lib`). The environment variable `NIEXTCCOMPILERSUPP` must point to the
  NI external compiler support directory, e.g.
  `C:\Program Files (x86)\National Instruments\Shared\ExternalCompilerSupport\C`.
* Microsoft Visual C++, CMake >= 3.13, and Boost.

## Building

### 1. Build whole project

```cmd
cd <root directory of vSomeIP-Lib>
mkdir build
cd build
cmake .. -A x64 -DCMAKE_INSTALL_PREFIX="$YOUR_PATH"
cmake --build . --config Release --target install
```

The top level build already contains the XNET examples, they are available in
`build/examples/xnet-examples/<example>/<target>/`.

### 2. Build a single example separately

```cmd
cd <root directory of vSomeIP-Lib>/examples/xnet-examples/<example>
mkdir build
cd build
cmake .. -A x64 -DCMAKE_PREFIX_PATH="$YOUR_PATH" -DUSE_TCP=OFF
cmake --build . --config Release
```

The build copies the matching JSON configuration next to the executable into a
`vsomeip/` sub folder, so the applications find their configuration automatically.

`USE_TCP` flag selects which configuration is deployed: `OFF` (default) deploys the
**UDP** configuration, `ON` deploys the
**TCP** configuration.

> Note: `xnet-notify-tp` has no `USE_TCP` option, SOME/IP-TP segmentation is only
> defined for UDP.


## Running

Start the server first, then the client (each in its own console, on the same machine or
on two machines connected to the same XNET network):

```cmd
<xnet examples build directory>\<example>\<example>-server\Release\<example>-server.exe
<xnet examples build directory>\<example>\<example>-client\Release\<example>-client.exe
```

Stop the applications with `Ctrl+C`.

## Expected result

Both applications first create their own XNET IP stack and wait until the interface is up:

```
Waiting for XNET IP stack to be ready...
Initializing XNET socket factory with XNET driver enabled...
```

`xnet-notify` - the server prints the notification value it sends, the client the value it received:

  ```
  # server              # client
  Notifying: 0          Received: 0
  Notifying: 1          Received: 1
  ```

`xnet-notify-tp` - the same, but with a large payload that is transported segmented:

  ```
  # server                             # client
  Notifying 4000 bytes data: 0         Received 4000 bytes data: 0
  ```

`xnet-request-responce` - request/response ping-pong:

  ```
  # client              # server
  Sending: ...          Received from 1313: ...
  Received: ...         Sending: ...
  ```
