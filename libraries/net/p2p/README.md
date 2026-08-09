# forge_net_p2p

`forge_net_p2p` is the peer-to-peer layer above transport sessions: peer identities,
sessions, protocol stream negotiation, peer exchange, relay reservations,
reachability probes, hole punching, path scoring, discovery protocol machinery
and GossipSub/pubsub.

## Current Support State

This library contains substantial libp2p-compatible protocol substrate, but it
is not yet a complete autonomous production host. Direct QUIC and TCP/Yamux,
secure peer authentication, session admission and connection management are on
the normal node path. GossipSub has bounded connected-peer mechanics and live
interop fixtures, but its overall support state remains `partial` until scoring
and autonomous topology are complete.

The following surfaces are not production claims yet:

- Kademlia, Rendezvous, Peer Exchange, Ping sampling and AutoNAT are explicit or
  inbound operations without complete node-owned maintenance;
- Kademlia routing currently uses persistent peer history instead of a bounded
  node-owned k-bucket table;
- standard Kademlia value operations do not have a value store or validation
  policy;
- AutoRelay and DCUtR mechanics lack the complete verified discovery and
  reachability feed;
- GossipSub scoring and autonomous topology remain incomplete;
- generic stream, dial and queued-byte resource scopes are not connected to all
  production paths.

The machine-readable support inventory is
[`p2p_feature_inventory.json`](../../../tests/libp2p_interop/p2p_feature_inventory.json).
It is the source inventory for the production-hardening program, not a record
of currently executed optional interop tests or a release-readiness verdict. A
`mapped` donor case names a compatibility fixture with declared Forge
coverage; it does not prove normal lifecycle activation or a passing current
donor run.

## When To Use

- Nodes need to connect by peer identity, not just host/port.
- Application protocols need named streams such as `/example/1`.
- Direct transports should be tried first, with explicit relay/hole-punch
  fallback.
- Application/plugin composition needs a shared P2P transport owner; use
  `forge::plugins::p2p::node` as the lifecycle/config/route facade above this
  low-level engine.

## When Not To Use

- Do not put application message semantics or storage semantics here.
- Do not treat P2P as authorization. Peer identity is transport identity;
  application authority is owned by consumers.
- Do not put application receipt, durable queue, storage or authorization semantics
  into peer networking. DHT, rendezvous, AutoRelay and GossipSub mechanics
  belong in `forge_net_p2p`; application protocols decide what an operation means.

## Public Modules

- `forge.net.p2p.identity`, `forge.net.p2p.endpoint`, `forge.net.p2p.node`.
- `forge.net.p2p.protocol`, `forge.net.p2p.message`, `forge.net.p2p.negotiation`.
- `forge.net.p2p.peer_store`, `forge.net.p2p.discovery`, `forge.net.p2p.dht`,
  `forge.net.p2p.rendezvous`.
- `forge.net.p2p.pubsub`.
- `forge.net.p2p.relay`, `forge.net.p2p.scoring`.
- `forge.net.p2p.exceptions`.

Target: `forge_net_p2p`.

Dependencies: `forge_api_core`, `forge_asio`, `forge_net_transport`, `forge_net_tcp`, `forge_net_quic`,
`forge_net_yamux`, `forge_multiformats`, Boost.Asio and, temporarily, RocksDB.
The direct RocksDB peer-store backend is an interim implementation scheduled
for replacement by an async persistence port and an ObjectDB adapter.

Foundation compatibility modules below P2P live in `forge_multiformats`:
`forge.multiformats.varint`, `forge.multiformats.multicodec`,
`forge.multiformats.multihash`, `forge.multiformats.multibase` and
first-class multiaddr/address support.

## Production Network Direction

`forge_net_p2p` is the owner for production peer-network mechanics. The direction is
a clean C++23 libp2p-compatible implementation: FORGE public types stay
FORGE/Boost-style, while supported libp2p protocols must be wire-compatible with
go-libp2p and rust-libp2p.

Compatibility is not a direct libp2p dependency and not a Go/Rust runtime clone.
It means the same peer identity model, address encoding, protocol negotiation,
handshake, protocol IDs and message rules for protocols FORGE marks as supported.

The canonical block order and donor test rules live in
[`docs/network/quic-p2p.md`](../../../docs/network/quic-p2p.md). Keep this README
as a library overview; do not duplicate the block sequence here.

Current direction: P2P sits above first-class multiaddr, reusable
`forge_net_transport`, and reusable TCP/STCP/Yamux/QUIC layers. QUIC and
TCP+TLS/Noise+Yamux direct paths are wired through private direct profiles.
Future transports must plug into the same multiaddr and transport session
boundary, not fork P2P core.

`forge_net_transport` is the stream/session substrate for `forge_net_p2p`; it is not an API
or RPC layer. API-over-stream serving lives in `forge.api.stream`, where QUIC/P2P
bindings share frame serve-loop logic without putting `forge::api` into
`forge_net_transport`.

Network-level behaviors that must not be pushed into plugins:

- relay-only/no-direct path support;
- independent maintenance scheduling for peer exchange, reachability, relay
  reservation renewal and discovery;
- peer discovery and relay discovery;
- protocol capability negotiation;
- network limits, backpressure, metrics and shutdown behavior.

`forge_net_p2p` remains free of application plugins, storage and authorization
policy. Application protocols own idempotency, acknowledgement and
permission checks above P2P.

GossipSub validation keeps `accept`, `reject` and `ignore` terminal while the
message remains in bounded history. `retry`, handler failure and local
validation backpressure are transient: the receiving heartbeat requests the
cached payload from its source peer after a capped exponential cooldown,
independently of ordinary `IHAVE` history. A message becomes terminally ignored
after the configured validation or request-attempt limit. Each heartbeat
applies a round-robin retry budget, and retry records are evicted with the
payload history, so unreachable peers and repeated transient failures cannot
create unbounded work or a second cache.

## Examples

### Start A Node

```cpp
#include <boost/asio/awaitable.hpp>

import forge.net.p2p.identity;
import forge.net.p2p.endpoint;
import forge.net.p2p.node;

boost::asio::awaitable<void> start_node(forge::asio::runtime& runtime) {
   auto options = forge::net::p2p::node::options{
      .certificate_pem = certificate_pem,
      .private_key_pem = private_key_pem,
      .peer_store_path = "/var/lib/forge/p2p/peer-store",
   };

   auto peer = forge::net::p2p::make_peer_id_from_certificate_pem(certificate_pem);
   auto node = forge::net::p2p::node{runtime, options};
   co_await node.async_listen(forge::net::p2p::parse_endpoint("/ip4/127.0.0.1/udp/9443/quic-v1"));
   advertise_peer(peer);
}
```

Production certificates must carry the signed libp2p identity extension. Peer
IDs are not derived from a bare certificate hash in production verification
paths.

### Parse A libp2p QUIC Endpoint

`forge::net::p2p::endpoint` is FORGE-style public vocabulary. It accepts and emits the
libp2p address text format for compatibility, but callers do not need to model
their application API around the `multiaddr` term.

```cpp
import forge.net.p2p.endpoint;

auto endpoint = forge::net::p2p::parse_endpoint(
   "/ip4/127.0.0.1/udp/4001/quic-v1/p2p/12D3KooW...");

co_await node.async_listen(endpoint);

co_await node.async_listen(forge::net::p2p::parse_endpoint("/ip4/127.0.0.1/tcp/4001"));
std::vector<forge::net::p2p::endpoint> advertised = node.local_endpoints();
```

QUIC and TCP+TLS/Noise+Yamux are currently registered direct transports. TCP
prefers libp2p TLS (`/tls/1.0.0`) and keeps Noise as fallback. `/ws` and `/wss`
multiaddrs are parseable but direct dial/listen returns typed unsupported until
a dedicated compatibility block wires a production transport. Future transports
must use the same private direct profile boundary.

`local_endpoints()` is the full canonical listen/advertise set and each endpoint
includes `/p2p/<local-peer>`. `local_endpoint()` remains a first-endpoint
compatibility convenience for older single-listen consumers.

### Peer Store Backends

The current low-level node requires a persistent peer store outside explicit
insecure tests. If `node::options` does not provide `peer_store_backend`,
`peer_store_path` opens the interim RocksDB backend. The official P2P plugin
does not yet provide this production dependency, and the backend performs
synchronous storage work and broad scans. Do not interpret its presence as the
completed production persistence design. The in-memory backend is only for
explicit tests and local insecure experiments.

```cpp
auto node = forge::net::p2p::node{runtime, {
   .certificate_pem = certificate_pem,
   .private_key_pem = private_key_pem,
   .peer_store_path = "/var/lib/forge/p2p/peer-store",
}};

auto test_store = forge::net::p2p::peer_store{
   {.backend = forge::net::p2p::peer_store::make_memory_backend()}};
```

### Register A Protocol

```cpp
#include <cstdint>
#include <vector>

node.register_protocol_handler(forge::net::p2p::protocol_id{.value = "/example/1"},
                               [](forge::net::p2p::node::incoming_protocol_stream incoming)
   -> boost::asio::awaitable<void> {
   std::vector<std::uint8_t> frame = co_await incoming.stream.async_read_frame();
   co_await incoming.stream.async_write_frame(frame);
});
```

### Publish Typed APIs Above P2P

Application protocols that need request/response, typed errors and idempotent
operation receipts should expose an `forge_api_core` contract and mount it through the
P2P API binding or `forge::plugins::p2p::resolver`. P2P opens the stream and
enforces peer/path policy; API dispatch owns method calls and error projection;
the application handler owns authorization and durable state.

### Typed API Protocol Binding

`forge.api.p2p.binding` builds P2P API bindings on top of negotiated protocol streams.
The binding path uses `multistream-select` and the same direct, hole-punch and
relay path manager as ordinary P2P protocol streams; it must not reintroduce an
FORGE-only hello envelope into direct QUIC sessions. Once a protocol stream is
open, frame serving delegates to `forge.api.stream`; P2P keeps only P2P policy:
protocol id, known-peer checks and discovery scope.

### Connect And Open A Protocol Stream

This is the low-level engine path for custom transport owners and tests.
Application plugins should use `forge::plugins::p2p::node::api` instead of calling these
methods directly.

```cpp
boost::asio::awaitable<void> open_example_stream(forge::net::p2p::node& node) {
   forge::net::p2p::node::session_info session = co_await node.async_connect(remote_endpoint, {
      .expected_peer = expected_peer,
      .timeout = std::chrono::milliseconds{10'000},
   });

   forge::net::p2p::stream stream = co_await node.async_open_protocol_stream(
      session.remote_peer,
      forge::net::p2p::protocol_id{.value = "/example/1"});
   use_stream(std::move(stream));
}
```

### Learn Endpoints And Probe Reachability

```cpp
import forge.net.p2p.peer_store;

node.peers().learn_endpoint(
   remote_peer,
   forge::net::p2p::parse_endpoint("/ip4/127.0.0.1/udp/9444/quic-v1"),
   {.bits = forge::net::p2p::capabilities::direct_quic | forge::net::p2p::capabilities::peer_exchange});

boost::asio::awaitable<void> update_reachability(forge::net::p2p::node& node) {
   forge::net::p2p::reachability::state reachability = co_await node.async_probe_reachability(observer_peer);
   if (reachability == forge::net::p2p::reachability::state::relay_only) {
      schedule_relay_setup(remote_peer);
   }
}
```

### Reserve Relay Explicitly

```cpp
boost::asio::awaitable<void> open_relayed_stream(forge::net::p2p::node& node) {
   forge::net::p2p::relay::reservation::info reservation = co_await node.async_reserve_relay(
      relay_peer,
      {.ttl = std::chrono::milliseconds{60'000}, .max_streams = 8});

   forge::net::p2p::stream relayed = co_await node.async_open_protocol_stream(
      remote_peer,
      forge::net::p2p::protocol_id{.value = "/example/1"},
      {.allow_relay = true, .relay_peer = reservation.relay_peer});
   use_stream(std::move(relayed));
}
```

### Stop Cleanly

```cpp
boost::asio::awaitable<void> stop_node(forge::net::p2p::node& node) {
   co_await node.async_stop();
}

void request_node_stop(forge::net::p2p::node& node) {
   node.stop();
}

boost::asio::awaitable<void> finish_node_stop(forge::net::p2p::node& node) {
   co_await node.async_stop();
}
```

`stop()` closes admission and listeners and starts disconnecting current
sessions without blocking the caller. It intentionally removes those sessions
from the active set before their transport teardown has finished.
`async_stop()` is the completion barrier: it always waits for the teardown
started by `stop()`, including STCP/Yamux read-loop cleanup.

## Security Notes

Production options require mTLS identity with a signed libp2p certificate
extension. `allow_insecure_test_mode` exists for tests and explicit local
experiments only; in that mode the node may use the in-memory peer store when no
path/backend is provided. Peer mismatch, TLS verification failure, missing
identity extension and invalid envelopes are correctness failures.

The node parses its configured identity key once during construction and reuses
the immutable key material for TLS, Noise, PubSub, rendezvous and relay
signatures. Insecure QUIC-only test nodes may omit signing material until an
operation that requires a signature is used.

## Risks And Anti-Patterns

- Do not treat peer identity as application authorization. It proves transport
  identity, not permission to perform application actions.
- Do not silently fall back to relay for operations that require a direct-peer
  policy. Relay use must be explicit and visible to the caller.
- Do not put durable delivery, exactly-once semantics or storage guarantees in
  `forge_net_p2p`; protocols above P2P own those contracts.
- Do not implement application retry or durable delivery loops against raw
  `node` in application plugins. Use typed request/receipt APIs for synchronous
  operations and a focused higher-level service for durable asynchronous work.
- Do not define a new P2P-only API error payload. API protocols use
  `forge::api::core::error_payload` in `forge::api::core::frame` error responses.
- Do not let protocol handler exceptions disappear in detached tasks. Expected
  application failures should be typed exceptions and unexpected failures should
  be counted/diagnosed.
- Do not treat `.peer_policy(...)` or `.max_inflight_per_peer(...)` as cosmetic.
  Unknown peers and too many active API calls are rejected before application API
  handlers run.
- Do not make `forge.api.p2p.binding` responsible for peer discovery, relay or node
  lifecycle. It is only the API protocol binding artifact.
- Do not implement AutoNAT, AutoRelay, DHT, rendezvous or pubsub loops in an
  infrastructure plugin. Network mechanics belong in `forge_net_p2p`; plugins only
  configure and consume them.

## Typical Mistakes

- Do not pass plaintext secrets through protocol IDs or peer metadata.
- Do not register duplicate protocol handlers; the node rejects them.
- Do not use relay fallback silently for actions that require direct peer policy.

## Tests

`test_forge_quic_p2p` covers identity shape, codec rejection, direct protocol echo,
path manager fallback, connect/open timeouts, peer exchange, relay, reachability,
hole punching, DHT/rendezvous component behavior and production option
validation.
