# Forge P2P DHT/Rendezvous Discovery v1 Donor Note

## Scope

This note tracks the first production-shaped discovery slice now owned by
`forge_net_p2p`.
The implementation adds owner modules for Kademlia-compatible DHT and
libp2p rendezvous, durable state through `peer_store`, node-level protocol
handlers selected through multistream-select, and the F.2 discovery lifecycle
hardening pass: iterative DHT lookup, provider publication to discovered
closest peers, rendezvous refresh/cookie semantics and discovery-backed
AutoRelay candidate learning.

Supported claims stay tied to evidence. The current slice has component proof
for wire codecs, FCL-to-FCL negotiated streams, routing/provider state and
rendezvous register/discover state, plus live donor interop artifacts for
DHT peer/provider lookup against go-libp2p and rust-libp2p and Rendezvous
register/discover against rust-libp2p.

## Donor Sources

| Area | Donor source | Accepted pattern | FCL target |
|---|---|---|---|
| Kademlia DHT | `donors/libp2p-specs/kad-dht/README.md` | XOR distance over `sha256(key)`, `k=20`, `alpha=10`, bounded query timeouts and closest-peer expansion | `forge.net.p2p.dht`, `dht::routing_table`, `dht_query`, `node::async_find_peer` |
| DHT wire messages | `donors/rust-libp2p/protocols/kad/src/generated/dht.proto`, `donors/go-libp2p-kad-dht/pb/dht.proto`, `donors/go-libp2p-kad-dht/handlers.go` | Length-delimited Protocol Buffers message with `FIND_NODE`, `ADD_PROVIDER`, `GET_PROVIDERS`; `ADD_PROVIDER` is send-message and validates provider peer equals stream peer | `dht::codec`, `node::impl::handle_dht`, `node::async_provide` |
| Rendezvous protocol | `donors/libp2p-specs/rendezvous/README.md` | `/rendezvous/1.0.0`, register/discover/unregister, TTL, namespace limits, cookie continuation | `forge.net.p2p.rendezvous`, `node::impl::handle_rendezvous` |
| Rendezvous wire messages | `donors/rust-libp2p/protocols/rendezvous/src/generated/rpc.proto`, `donors/rust-libp2p/protocols/rendezvous/src/codec.rs` | Proto2 message types, status codes, signed PeerRecord and cookie format | `rendezvous::codec` |

## Accepted Rules

- DHT and rendezvous mechanics live in `forge_net_p2p`, not in the official
  P2P plugin.
- Public API stays owner-shaped: `dht::options`, `dht::query_result`,
  `rendezvous::options`, `rendezvous::registration`, `discovery::policy`.
- `peer_store::persistence` is backend-neutral and asynchronous. The official
  plugin owns its private ObjectDB adapter; `forge_net_p2p` has no RocksDB or
  DB Store dependency.
- DHT/rendezvous messages are full length-delimited libp2p protocol payloads;
  payload-only helpers are not public API.
- Live support claims require matching artifacts from
  `test_forge_libp2p_interop` and do not follow from codec tests alone.

## Current Proof

| Case | Status | Proof |
|---|---|---|
| DHT protocol id | Ported | `p2p_libp2p_reachability_relay_protocol_ids_are_exact` |
| DHT codec and malformed rejection | Ported | `p2p_dht_codec_roundtrips_libp2p_message_shape_and_rejects_malformed` |
| DHT k-bucket bounds, replacement and XOR ordering | Ported | `p2p_dht_k_bucket_bounds_active_and_replacement_capacity`, `p2p_dht_k_bucket_closest_is_sha256_xor_ordered_and_deterministic` |
| DHT node handler over negotiated stream | Ported | `p2p_dht_node_finds_peer_and_provider_over_negotiated_stream` |
| DHT iterative many-peer lookup | Ported | `p2p_dht_iterative_lookup_walks_many_peer_topology` |
| DHT iterative provider lookup and provide | Ported | `p2p_dht_iterative_provider_lookup_and_provide_reach_closest_peers` |
| DHT bounded async persistence | Ported | `p2p_peer_store_memory_persistence_hydrates_bounded_pages`, `p2p_peer_store_bounds_pending_queue_and_recovers_after_flush` |
| DHT ObjectDB reopen through official plugin | Ported | `p2p_node_plugin_production_lifecycle_reopens_persisted_peer_state`, conditional MDBX/RocksDB reopen cases |
| DHT live peer lookup fixture | Limited | `test_forge_libp2p_interop dht_find_peer`; direct-peer setup is not credited as outbound iterative lookup proof |
| DHT live provider lookup | Ported | `test_forge_libp2p_interop dht_provide_find_provider` against go-libp2p/rust-libp2p |
| Rendezvous protocol id | Ported | `p2p_libp2p_reachability_relay_protocol_ids_are_exact` |
| Rendezvous codec, TTL, cookie and status | Ported | `p2p_rendezvous_codec_roundtrips_register_discover_cookie_and_status` |
| Rendezvous node handler over negotiated stream | Ported | `p2p_rendezvous_node_registers_and_discovers_over_negotiated_stream` |
| Rendezvous refresh, replacement and cookie continuation | Ported | `p2p_rendezvous_refresh_replaces_registration_and_cookie_discovers_new_records` |
| Rendezvous durable registration state | Ported | async persistence fixtures and official-plugin ObjectDB reopen coverage |
| Rendezvous live register/discover | Ported | `test_forge_libp2p_interop rendezvous_register_discover` against rust-libp2p |
| Discovery refresh feeds AutoRelay | Ported | `p2p_discovery_refresh_learns_dht_and_rendezvous_relay_candidates_for_autorelay` |

## Unsupported Gaps

- Live donor fixtures for repeated many-peer DHT/rendezvous refresh topologies
  are still limited. Forge component simulations cover the lifecycle; live matrix
  artifacts remain peer/provider lookup and Rust rendezvous register/discover.
- Automatic long-running local provider republish and withdrawal ownership is
  Stage 4 work. Explicit `async_provide(...)` proves the current caller-driven
  path but is not a production lifetime claim.
- Standard Kademlia value operations remain unsupported until validators,
  selectors, conflicts, expiry and bounded persistence are implemented. Forge
  does not advertise an echo-style value stub as support.
- Go Rendezvous behaviour proof is not claimed because no official go-libp2p
  rendezvous behaviour donor is present in the workspace.

These gaps are also tracked in `tests/libp2p_interop/donor_cases.json`.
They must not be described as supported until matching donor-derived tests and
live artifacts are produced.

GossipSub/pubsub is tracked separately in
`docs/donors/fcl-p2p-gossipsub-v1.md` and is no longer a DHT/Rendezvous gap.
