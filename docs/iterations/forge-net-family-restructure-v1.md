# Forge Net Family Restructure v1

Status: future direction. This note records the agreed shape only. It is not
implemented in the current branch.

## Goal

After the current DB-family work is finished, the network libraries should move
to the same physical-path, namespace, module and target discipline:

- physical path follows namespace/module path;
- intermediate grouping namespaces are empty;
- public types live in leaf namespaces;
- no compatibility aliases, forwarding modules or duplicate public surfaces.

The agreed family root is `forge::net`, not `forge::network`.

## Why `net`

`net` is shorter and keeps long C++ API names readable:

```cpp
forge::net::http::client
forge::net::websocket::connection
forge::net::p2p::node
forge::net::quic::endpoint
```

`network` does not add useful meaning, but it makes every type and module name
heavier.

## Target Shape

Namespaces:

```text
forge::net::transport
forge::net::tcp
forge::net::stcp
forge::net::quic
forge::net::yamux
forge::net::http
forge::net::websocket
forge::net::p2p
```

Targets and components:

```text
forge_net_transport / net_transport
forge_net_tcp       / net_tcp
forge_net_stcp      / net_stcp
forge_net_quic      / net_quic
forge_net_yamux     / net_yamux
forge_net_http      / net_http
forge_net_websocket / net_websocket
forge_net_p2p       / net_p2p
```

Modules:

```text
forge.net.transport.*
forge.net.tcp.*
forge.net.stcp.*
forge.net.quic.*
forge.net.yamux.*
forge.net.http.*
forge.net.websocket.*
forge.net.p2p.*
```

Physical layout:

```text
libraries/net/
  CMakeLists.txt
  transport/
  tcp/
  stcp/
  quic/
  yamux/
  http/
  websocket/
  p2p/
```

`libraries/net/CMakeLists.txt` should be a dispatcher only. Each child remains a
real library target with its own public modules, implementation files, README,
tests and package component.

## Migration Boundary

This must be one breaking network-family refactor, not a gradual mix of old and
new public surfaces. A partial rename such as `forge::net::p2p` while keeping
`forge::http` would make the API less coherent than it is today.

Old names to remove in the implementation pass:

- `forge::transport`, `forge::tcp`, `forge::stcp`, `forge::quic`,
  `forge::yamux`, `forge::http`, `forge::websocket`, `forge::p2p`;
- `forge_transport`, `forge_tcp`, `forge_stcp`, `forge_quic`,
  `forge_yamux`, `forge_http`, `forge_websocket`, `forge_p2p`;
- `forge.transport.*`, `forge.tcp.*`, `forge.stcp.*`, `forge.quic.*`,
  `forge.yamux.*`, `forge.http.*`, `forge.websocket.*`, `forge.p2p.*`;
- package components without the `net_` prefix.

No old CMake aliases, package aliases, forwarding modules or namespace aliases
should be added.

## Non-goals

This rename must not change protocol behavior, wire formats, stream semantics,
plugin ids, config sections or network runtime policy.

It also must not introduce a monolithic `forge_net` runtime library. The family
root is an empty grouping namespace and filesystem dispatcher, not an umbrella
target that hides dependency edges.

## Validation Shape

The implementation pass should prove:

- all network packages build and install under `net_*` components;
- package consumers import `forge.net.*` modules only;
- plugins and tests no longer import or reference old root network modules;
- `create-library` layout gates pass for every child under `libraries/net`;
- behavior suites for HTTP, WebSocket, QUIC, P2P, TCP/STCP, Yamux and transport
  remain green.
