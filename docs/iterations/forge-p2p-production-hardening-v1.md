# Forge P2P Production Hardening v1

> **Status:** accepted priority direction, implementation pending.
>
> This document records the production integration gaps found while preparing
> Forge Content Swarm. It does not change the public API by itself.

## 1. Audit Standard

A libp2p feature is not considered delivered merely because its codec, protocol
handler, low-level `forge_net_p2p` method or interoperability test exists. A
production claim additionally requires:

- configuration through the official `plugins.p2p.node` surface;
- activation and deterministic shutdown through the plugin lifecycle;
- bounded scheduling, cancellation, retry and backoff;
- diagnostics that expose the effective state;
- an integration test that starts official plugins rather than raw test nodes.

The low-level implementations remain valuable and should be reused. The gap is
primarily host/plugin orchestration, not a request to introduce a second P2P
stack.

## 2. Current Reliable Baseline

The following paths are connected through the official plugin today, subject to
the production-startup gap below:

- direct QUIC and TCP/Yamux sessions to configured or already known peers;
- bounded bootstrap reconnect and bootstrap-session protection;
- inbound application protocol routing and Forge API over a known `peer_id`;
- session, stream and resource admission limits;
- diagnostics over the shared node;
- GossipSub publish/subscribe over peers that are already connected.

This is a static/bootstrap-centric topology. It is not yet an autonomous
libp2p mesh.

## 3. Priority Findings

### P0: Production Node Startup And Persistence

`forge_net_p2p` correctly requires a persistent peer store outside insecure
test mode. The official plugin neither exposes a peer-store path/backend in its
configuration nor supplies one when constructing the node. Consequently, the
configured plugin cannot currently satisfy the production node contract; its
integration tests use insecure test mode.

Required outcome:

- expose a product-neutral persistent peer-store configuration;
- open and close it through the plugin lifecycle;
- reject invalid or unwritable storage before network admission opens;
- retain learned peers, endpoint observations, DHT/provider records,
  Rendezvous registrations, relay reservations and backoff state across restart;
- add a secure official-plugin startup/reopen test without
  `allow-insecure-test-mode`.

### P1: Identify And Remote Capability Truth

Forge responds to Identify and accepts Identify Push, but an ordinary bootstrap
or direct connection does not initiate Identify. Identify Push is not emitted
when the local address/protocol set changes. New session records are initially
populated from local capabilities rather than verified remote capabilities.

Required outcome:

- run bounded Identify after each newly established session;
- treat TLS/Noise peer authentication and Identify protocol advertisement as
  separate facts;
- populate session and peer-store capabilities only from the remote document;
- emit Identify Push after a relevant local endpoint or protocol change;
- reject or quarantine inconsistent peer/address/capability observations;
- prove Forge-to-Forge capability learning through official plugins.

### P1: Discovery Lifecycle And Topology Maintenance

Kademlia DHT, provider records and Rendezvous are implemented in
`forge_net_p2p`, but the official plugin does not enable their capabilities,
configure their operating roles or call `async_refresh_discovery()`. Discovery
results alone are also insufficient: the host must maintain a bounded set of
useful sessions.

Required outcome:

- add explicit DHT client/server and Rendezvous client/server configuration;
- start an initial discovery pass after bootstrap connectivity;
- run one cancellable refresh loop with TTL-aware refresh, bounded parallelism,
  retry and jittered backoff;
- maintain configurable low/target/high peer watermarks;
- select and dial scored discovered peers without displacing protected
  bootstrap sessions;
- expire stale observations and disconnect obsolete unprotected sessions;
- stop discovery and dialing deterministically before node teardown.

Products may disable decentralized discovery for a deliberately static
deployment, but the mode must be explicit and diagnostics must report it.

### P1: Peer Exchange Activation

The Peer Exchange protocol can answer inbound requests and its capability is
advertised, but official Forge nodes never initiate an exchange. Therefore it
does not currently expand a Forge-to-Forge topology.

Required outcome:

- request Peer Exchange from a bounded subset of identified compatible peers;
- trigger it during initial topology formation and at a bounded refresh rate;
- retain existing endpoint sanitization, record limits and per-peer backoff;
- feed accepted records into the same topology manager as DHT and Rendezvous;
- prove that a node learns and connects to a non-bootstrap peer.

### P1: Reachability, AutoRelay And Hole Punching

AutoNAT handlers and manual probing exist, while AutoRelay maintenance is
started automatically. The plugin does not schedule reachability probes or
expose their policy. AutoRelay is normally starved of candidates because
Identify, DHT, Rendezvous and Peer Exchange do not populate the production peer
set. Circuit Relay v2 and DCUtR hole punching therefore work mainly in focused
raw-node/interoperability tests.

Required outcome:

- configure trusted AutoNAT observers and a bounded re-probe policy;
- aggregate observations instead of trusting one peer;
- publish effective public/private/unknown reachability in diagnostics;
- feed verified relay-capable peers into the existing AutoRelay loop;
- renew and replace reservations before expiry;
- attempt DCUtR only with valid relay context and preserve relay fallback;
- prove direct, relayed and hole-punched official-plugin paths, including
  reservation expiry and peer loss.

The existing Relay, AutoRelay and DCUtR mechanics should be completed through
orchestration rather than duplicated in plugins.

### P2: Liveness And Operational Maintenance

Ping supports inbound responses and explicit RTT measurements, but no official
plugin policy performs periodic health sampling. Connection closure is observed,
yet diagnostics do not provide an actively maintained peer-health view.

Required outcome:

- define whether a deployment enables bounded periodic Ping sampling;
- avoid a mandatory ping storm for idle or very large networks;
- feed successful RTT and failures into existing scoring/backoff state;
- expose last successful contact, RTT and failure/backoff status;
- distinguish responder-only Ping support from active health monitoring in docs.

### P2: GossipSub Topology Completeness

The official PubSub plugin is operational over connected peers and its
heartbeat, validation and resource bounds are active. Its mesh cannot become
independent of configured bootstrap/static sessions until discovery and
topology maintenance are completed.

Required outcome:

- do not rewrite GossipSub;
- allow the shared topology manager to supply identified compatible peers;
- verify mesh repair after bootstrap loss and discovered-peer replacement;
- keep PubSub delivery explicitly non-durable and non-exactly-once.

## 4. Ownership Boundaries

`forge_net_p2p` continues to own protocol codecs, peer store, Identify, Ping,
AutoNAT, DHT, Rendezvous, Relay, DCUtR, GossipSub, scoring and resource-manager
mechanics.

`plugins.p2p.node` owns configuration, lifecycle orchestration, maintenance
tasks, the shared node and narrow local APIs for other official plugins. It must
not expose an unrestricted raw-node escape hatch.

Product plugins own authorization, network/realm membership, application
protocols and business routing. `plugins.p2p.resolver` continues to open a typed
API on an already known peer; it does not become peer or content discovery.

## 5. Implementation Order

1. Make secure production startup possible with persistent peer-store ownership.
2. Correct Identify and remote capability learning.
3. Add one discovery/topology lifecycle for DHT, Rendezvous and Peer Exchange.
4. Activate AutoNAT observations and prove AutoRelay/DCUtR through the official
   plugin path.
5. Add focused local discovery APIs needed by consumers, beginning with bounded
   DHT provider publication/lookup for Content Swarm.
6. Complete liveness diagnostics and autonomous GossipSub mesh repair.

Each block requires an official-plugin integration test. Low-level raw-node
tests remain necessary but are not sufficient acceptance evidence.

## 6. Production Acceptance Gates

- A secure node starts with real identity material and persistent peer storage.
- Three nodes given only bootstrap entry points discover and connect to each
  other within bounded time.
- A restarted node restores valid peer/discovery state and safely expires stale
  records.
- DHT peer/provider lookup and Rendezvous discovery run through official plugin
  lifecycle and stop cleanly.
- Peer Exchange discovers a non-bootstrap node without accepting non-routable or
  identity-mismatched endpoints.
- Remote session capabilities match the peer's actual advertised protocols.
- Public and private reachability lead to the expected direct, relay and DCUtR
  paths.
- Loss of bootstrap, relay or a discovered peer repairs topology without an
  unbounded retry/task/memory increase.
- GossipSub continues delivery after bootstrap loss when other mesh peers remain.
- Diagnostics identify disabled, idle, degraded and healthy discovery states.
- Live Go/Rust libp2p interoperability remains green for every enabled protocol.

## 7. Non-Goals

- product authorization or chain/network membership;
- content-provider key design, seeding policy or transfer scheduling;
- replacing Forge API or `plugins.p2p.resolver`;
- a second peer database outside `forge_net_p2p`;
- unbounded background scanning, dialing, pinging or task creation.
