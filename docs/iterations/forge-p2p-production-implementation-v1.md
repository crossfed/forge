# Forge P2P Production Implementation v1

> **Status:** implementation roadmap.
>
> This document sequences the accepted findings in
> `forge-p2p-production-hardening-v1.md`. It does not replace that audit and
> does not by itself claim that a feature is production-ready.

## 1. Objective

Bring `forge_net_p2p` and the official P2P plugins to one production path in
which every advertised protocol is either:

- owned by the normal node lifecycle and proven through the official plugin;
- intentionally caller-driven and documented as such; or
- rejected as unsupported without a nominal-success stub.

Production means more than a compatible codec. The node must own bounded
state, activation, maintenance, resource reservations, persistence,
diagnostics and deterministic shutdown. The machine-readable inventory in
`tests/libp2p_interop/p2p_feature_inventory.json` is the support-claim source of
truth during this program.

## 2. Invariants

- `forge::net::p2p::node` owns transport-neutral libp2p mechanics.
- Official plugins adapt configuration and Forge application dependencies;
  they do not implement a second host lifecycle.
- Product protocols own authorization and application semantics.
- Durable history is not an operational routing table.
- Network-controlled queues, caches, queries and maintenance batches are
  bounded and observable.
- Codec, component and donor fixture evidence cannot by themselves establish a
  production claim.
- Standard `/ipfs/kad/1.0.0` support means peer, provider and validated value
  operations. A provider-only profile must use a product-owned protocol ID and
  must not negotiate the standard full-profile ID.
- A handler must never return protocol success for an operation whose state
  contract was not performed.

## 3. Evidence Gates

Evidence is cumulative:

1. exact codec and malformed-input fixtures;
2. bounded state-machine and shutdown tests;
3. raw-node lifecycle integration;
4. official-plugin configuration and lifecycle integration;
5. restart, scale and bounded-work proof;
6. adversarial admission and resource proof;
7. live Go and Rust libp2p interoperability.

An inventory item may be `live` only when its activation, owner, resource
release, diagnostics and applicable evidence are explicit. `mapped` in the
donor matrix means that a donor case has a Forge test; it does not mean that the
feature is activated or production-ready.

## 4. Stage 1: Support Claims And Inventory

### Scope

- enumerate every built-in and transport-upgrade protocol ID, capability bit
  and public nested operational component;
- classify every public P2P declaration through its exact owning module or
  macro-only header and that surface's feature mapping; a source digest covers
  the complete type, method and option text, so any declaration change requires
  an explicit inventory update and reclassification;
- verify that each public module root is registered to its canonical CMake
  target, component and module prefix;
- classify protocol, topology, persistence, resource and plugin surfaces with
  the accepted state vocabulary;
- record normal activation, owner, resource lifetime, persistence,
  maintenance, diagnostics, donor evidence and intended disposition;
- separate wire/component evidence from production support;
- correct current READMEs and donor metadata where wording overstates the
  normal application path.

### Exit gate

- source-only inventory check passes;
- every built-in/negotiated protocol, capability and tracked public nested
  component is covered exactly once;
- every public P2P module belongs to exactly one canonical target and its
  complete declaration snapshot matches the inventory;
- no unknown state, missing owner or orphan with a claimed owner exists;
- every `live` claim has raw-node, official-plugin and donor evidence;
- live scenarios use exact runner profile/scenario selectors, local donor
  documents resolve, and external donor files match pinned donor revisions
  whenever the donor tree is supplied;
- no README promotes a manual, partial, stub or orphan surface as a complete
  production feature.

## 5. Stage 2: Peer-State Foundation

> **Implementation status:** the Stage 2 foundation is implemented by
> `forge-p2p-peer-state-v1`. Async persistence, bounded operational state,
> k-bucket admission, the private ObjectDB adapter and secret-backed official
> plugin startup are covered by focused memory, P2P and DB Store suites. This
> status does not promote Kademlia or the node to `live`: autonomous refresh,
> complete Identify ownership and standard value operations remain Stage 3/4.

### 5.1 Async persistence boundary

Replace the synchronous peer-store backend with an asynchronous persistence
port owned by `forge_net_p2p`. The port supports paged hydration, atomic
batches, bounded expiry pruning, flush and deterministic close. It exposes P2P
domain records, not a database driver API.

Expiry pruning reports exact removed identities rather than category counts,
and durable mutation outcomes distinguish commit failure from a committed
mutation whose sync acknowledgement is uncertain. This keeps operational state
aligned with the database while surfacing the uncertainty as a typed failure.
That diagnostic remains degraded until a later durable apply or explicit flush
confirms durability; ordinary pruning cannot clear it. Provider and Rendezvous
records use the same per-peer endpoint and total variable-byte bounds as the
peer directory, both for live writes and hydration.

Remove the private RocksDB codec and direct `forge_rocksdb` dependency. Keep a
deterministic in-memory implementation for unit and programmatic test paths.

### 5.2 Bounded operational state

The in-memory peer directory provides point lookup and bounded indexed
candidate selection. High-frequency observations use one node-owned bounded,
coalescing persistence queue. Writes acknowledged to a remote peer, including
accepted provider and Rendezvous registrations, complete durably before the
response.

Queue saturation or persistence failure produces backpressure and degraded
diagnostics. No detached task may outlive the node.

### 5.3 Kademlia routing state

Replace the flat routing table with node-owned k-buckets:

- configured `k` and deterministic XOR-distance ordering;
- bounded replacement lists;
- Identify and hydrated records remain candidates until a successful validated
  DHT exchange admits the peer as a server;
- failure-driven replacement and eviction;
- bounded closest-peer and diagnostic snapshots;
- startup candidates revalidated before admission.

Iterative query seeds and `FIND_NODE` responses use this routing state, never a
full durable-store scan.

### 5.4 ObjectDB adapter

`plugins.p2p.node` owns private ObjectDB adapter components over a dedicated
named `plugins.db.store` Object layer. Schema v1 stores peer and endpoint facts,
Identify data, provider records, Rendezvous registrations, relay metadata and
monotonic sequence state. Indexes cover hydration priority, expiry, provider
key, namespace/sequence and uniqueness.

Read-modify-write and sequence changes share one ObjectDB transaction. Schema
mismatch fails startup with a typed error. The v1 recovery path is explicit
cache removal followed by bootstrap hydration; no implicit migration or full
startup scan is allowed.

### 5.5 Official plugin foundation

The plugin depends on DB Store and Crypto Secrets. During `after_initialize()`
it registers private Object models. During `startup()` it loads identity
secrets, opens and hydrates persistence, constructs the node, and only then
opens listeners. Shutdown reverses that ownership.

Configuration uses `peer-store.store`, `identity.certificate-secret` and
`identity.private-key-secret`. Inline PEM is removed from plugin configuration;
programmatic low-level identity construction remains supported. Insecure memory
mode is an explicit test path only.

### Exit gate

- memory and ObjectDB persistence fixtures have parity;
- MDBX and RocksDB reopen, commit and rollback paths pass;
- hydration, pruning, queueing and diagnostics remain bounded with durable
  history much larger than live routing capacity;
- no listener opens before secrets, schema and hydration succeed;
- closest-peer operations do not scan ObjectDB;
- the old RocksDB backend API and handwritten codec are absent.

Current focused evidence includes
`p2p_peer_store_memory_persistence_hydrates_bounded_pages`,
`p2p_dht_k_bucket_bounds_active_and_replacement_capacity`,
`p2p_dht_iterative_lookup_walks_many_peer_topology` and
`p2p_node_plugin_production_lifecycle_reopens_persisted_peer_state`.

## 6. Stage 3: Node Lifecycle, Identify And Resource Ownership

> **Implementation status:** implemented by `forge-p2p-node-lifecycle-v1`.
> The node now owns bounded bootstrap and maintenance, automatic per-session
> Identify and coalesced full-snapshot Identify Push, move-only network
> reservations, transport-backed queued-byte accounting and tracked shutdown.
> This status does not promote Kademlia, topology or the complete host to
> production readiness; their Stage 4/5 gates remain unchanged.

- move bootstrap startup and maintenance from the plugin into the node;
- apply one bounded startup budget, bounded parallelism, jittered retry and
  deterministic cancellation;
- run Identify after every authenticated session and persist only verified
  remote protocol/address facts;
- emit Identify Push after local protocol or address changes;
- replace manual stream/dial counters with move-only reservations on every
  inbound and outbound path;
- connect queued-byte accounting to real buffers or remove the claim;
- make maintenance use direct bounded state rather than diagnostics snapshots;
- expose effective limits, modes and degradation reasons without using
  diagnostics as control state.

### Exit gate

- raw and official-plugin sessions learn the same verified remote facts;
- all dial/stream/queue resources release on success, error, cancellation and
  shutdown;
- bootstrap and Identify continue to work for programmatic node users without
  the official plugin;
- adversarial limits and reverse shutdown are proven.

Focused evidence includes
`p2p_node_strict_bootstrap_retries_until_shared_startup_budget`,
`p2p_node_connect_waits_for_identify_and_push_replaces_protocol_snapshot`,
`p2p_node_identify_failure_keeps_authenticated_session_usable`,
`p2p_node_queued_byte_budget_is_held_until_quic_ack` and
`yamux_retains_chunk_lifetime_while_flow_control_is_blocked`.

## 7. Stage 4: Complete Kademlia And Durable Record Lifetimes

### Standard profile

When `/ipfs/kad/1.0.0` is enabled, Forge implements:

- peer lookup and routing-table refresh;
- provider add/get, bounded durable storage, expiry and republish lifetime;
- value put/get with application-selected validators and selectors, conflict
  handling, expiry and bounded persistence.

`PUT_VALUE` echo behavior is removed before the profile can be enabled. A node
without a value validator/store rejects value operations deterministically and
does not advertise the standard full profile.

### Provider registration lifetime

Providing is represented by an owned registration that supports renewal,
withdrawal and shutdown. The node owns TTL republish while the caller owns the
decision that content remains available.

### Provider-only deployments

A product may define a peer/provider-only DHT, but it must use a distinct
product protocol ID and explicit compatibility documentation. It cannot claim
interoperability with the standard Kademlia profile.

### Exit gate

- peer, provider and value paths pass codec, raw-node, restart, adversarial and
  live donor tests;
- provider and value records have bounded storage and maintenance;
- no nominal-success stub remains;
- routing refresh and queries remain independent of durable-history size.

The current `dht_find_peer` fixture connects directly to the searched peer and
can be satisfied from the local peer store. It is not live outbound DHT evidence
until a third-peer route or explicit Kademlia negotiation is proven.

## 8. Stage 5+: Production Host Completion

### Stage 5: Topology and discovery

Integrate bounded DHT, Rendezvous and Peer Exchange refresh into one node-owned
topology manager with low/target/high session watermarks. Static topology is an
explicit supported mode, not an accidental default.

### Stage 6: Reachability and path management

Maintain AutoNAT observations, effective reachability, AutoRelay candidates and
reservations, and one per-peer DCUtR attempt state machine. Preserve relay
fallback and prove reservation loss/renewal behavior.

### Stage 7: GossipSub and plugin surface

Complete donor-consistent scoring, decay, thresholds, mesh selection,
opportunistic grafting and score retention. The pubsub plugin remains a narrow
facade over node-owned GossipSub and topology. GossipSub v1.0 fallback needs a
fixture that forces v1.0 negotiation; successful v1.1 interop is not evidence
for the fallback protocol.

### Stage 8: Production proof

Run restart, churn, scale, hostile-peer, bounded-memory and long-duration tests
through both the raw node and official plugins, followed by live Go and Rust
interop. Only then may inventory entries be promoted to `live` and Content
Swarm resume on the hardened substrate.

## 9. Delivery Discipline

- Each stage is a focused PR against current `dev`.
- Runtime PRs follow `create-library` and `create-plugin` exact ownership.
- One writable worktree and one build tree are used for this program.
- Only the coordinator builds, with `-j4`; reviewers remain read-only.
- Full CI is reserved for release gates. Focused suites prove each stage.
- Changes to wire compatibility, public support claims or persisted schemas are
  explicit in the corresponding PR and migration note.
