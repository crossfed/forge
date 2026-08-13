# Forge P2P Node Lifecycle v1 Donor Note

## Scope

This note traces Stage 3 node lifecycle, Identify and network resource ownership.
It does not claim complete Kademlia, topology, AutoNAT/relay discovery or
GossipSub production readiness.

## Pinned Sources

| Area | Donor | Accepted pattern | Forge owner |
|---|---|---|---|
| Identify | Go libp2p `9cfe2cc0`, `p2p/protocol/identify/id.go` | Run Identify for authenticated connections; coalesce full local snapshots into bounded Identify Push work. | `identify_service`, `node_impl_identify.cpp` |
| Multipart Identify | Go libp2p `9cfe2cc0`, `p2p/protocol/identify/id.go` | Accept up to ten independently bounded 8 KiB protobuf frames and merge the base document with the signed-record continuation. | `node_impl_identify.cpp`, `identify.cpp` |
| Identify errors | Rust libp2p `22fb4c78`, `protocols/identify/src/handler.rs` | Treat malformed, timeout and unsupported Identify as stream/session facts; do not fabricate remote capabilities. | per-session `identify::state`, `identify_error` |
| Identify Push merge | Rust libp2p `22fb4c78`, `protocols/identify/src/protocol.rs` | Preserve scalar and repeated fields omitted by a partial Push; replace fields actually carried by the update. | `identify::document_presence`, `node_impl_identify.cpp` |
| Signed peer records | Go libp2p `9cfe2cc0`, `core/peer/record.go`; Rust libp2p `22fb4c78`, `core/src/peer_record.rs` and `protocols/identify/src/handler.rs` | Emit the canonical `libp2p-peer-record` / `03 01` envelope for Identify. Accept the exact legacy `libp2p-routing-state` profile still emitted by the pinned Rust Identify and Rendezvous implementations. | `node_impl_identify.cpp`, `rendezvous::codec` |
| Resource ownership | Go libp2p `9cfe2cc0`, `core/network/rcmgr.go` | Acquire provisional scopes before expensive work, bind after peer/protocol negotiation and release by ownership. | `resource_manager` move-only reservations, `resource_stream` |
| Circuit Relay v2 limits | libp2p Circuit Relay v2 specification | Enforce duration per relayed connection and data independently in each direction; crossing either limit resets both circuit streams. | `relay_pair`, `relay_budget`, `node_impl_relay.cpp` |
| QUIC send lifecycle | ngtcp2 `1.22.1`, `ngtcp2_conn_writev_stream`, `ngtcp2_conn_handle_expiry` and packet TX-time contracts | Match the native TX packet limit, serialize FIN before completing close, retain queued-byte ownership through ACK/reset, timestamp socket handoff and treat idle expiry as an orderly close. | `quic_engine.cpp` |
| QUIC execution ownership | ngtcp2 `1.22.1` connection and stream APIs | Execute every operation that reads or mutates one native connection on that connection's owner strand; a caller executor is only the continuation destination. | `quic_engine.cpp` |
| QUIC Retry validation | ngtcp2 `1.22.1`, example server `send_retry()` and `verify_retry_token()` | Validate unknown Initial addresses with an encrypted ten-second Retry token before allocating TLS/native connection state; carry the verified original DCID and Retry SCID into server transport parameters. | `quic_engine.cpp` |

Canonical source links:

- <https://github.com/libp2p/go-libp2p/blob/9cfe2cc00be5b20a0be737f002c99f81b92255c5/p2p/protocol/identify/id.go>
- <https://github.com/libp2p/rust-libp2p/blob/22fb4c784fc55ad8b15d05fdc9f98d663107d4cb/protocols/identify/src/handler.rs>
- <https://github.com/libp2p/go-libp2p/blob/9cfe2cc00be5b20a0be737f002c99f81b92255c5/core/network/rcmgr.go>
- <https://github.com/libp2p/specs/blob/master/relay/circuit-v2.md>

## Accepted Rules

- `forge::net::p2p::node` owns hydration, listeners, bounded initial bootstrap,
  retries, Identify, protocol snapshots and deterministic stop. The official
  plugin only adapts config, Secrets and ObjectDB persistence.
- Authenticated transport identity is not protocol capability evidence. A new
  session starts with empty remote capabilities and replaces them only after a
  verified Identify document.
- Identify failure leaves the authenticated session usable with unknown remote
  capabilities. Product protocols may still be negotiated explicitly.
- `identify::limits::max_message_size` bounds one length-delimited donor frame;
  `max_total_message_size` independently bounds the merged multipart protobuf.
  The default accepts the Go donor's ten 8 KiB parts while reserving decode
  memory before reading.
- Rust-compatible partial Push updates do not clear omitted scalar fields or
  empty repeated fields. Forge's coalesced full snapshot always contains the
  built-in protocol set, so protocol removals still replace the prior list.
- A valid certified PeerRecord with the same sequence is accepted as a refresh.
  Rust sequence values have second granularity and Go's certified address book
  likewise permits equal-sequence refreshes; only regressions are rejected.
- Identify and Rendezvous share the PeerRecord protobuf payload, not its signed
  envelope profile. Forge emits the cross-implementation canonical Identify
  domain and two-byte payload type. The pinned Rust Identify and Rendezvous
  implementations still emit the legacy domain and textual payload type, which
  Forge accepts by exact payload-type dispatch. Accepting either profile never
  means verifying one signature against a list of arbitrary domains.
- A local protocol handler is published only if the resulting full Identify
  snapshot fits the configured outbound limit. Multipart input is committed
  only after clean stream EOF; reset and cancellation discard every accumulated
  part instead of publishing a truncated peer document.
- Bootstrap peer IDs are optional compatibility pins. When present they are
  verified during authentication; when absent the authenticated session peer is
  learned and protected only after the connection succeeds.
- A stream reservation is provisional before multistream negotiation and bound
  to `{peer, protocol}` afterward. Dial and session reservations cover one
  logical owner rather than individual fallback attempts.
- Queued-byte ownership follows the transport: Yamux releases after serialized
  flow-controlled write completion; QUIC releases after ACK, reset or close.
  QUIC stream close itself completes only after FIN is serialized into a native
  packet; loss and retransmission remain owned by ngtcp2.
- Circuit Relay duration is enforced with the same whole-second value advertised
  on the Relay v2 wire. Byte limits belong to one circuit and apply independently
  in each direction; exact exhaustion closes that direction without waiting for
  an extra byte. Renewal preserves the reservation generation and active-circuit
  count, while loss of the last authenticated session releases the reservation.
  Duration or byte exhaustion closes both circuit streams and releases the scoped
  relay reservation.
- Graceful QUIC shutdown emits a standard application `CONNECTION_CLOSE` before
  local socket teardown. The remote lifecycle can therefore release session and
  relay ownership immediately instead of waiting for idle expiry.
- QUIC listener admission follows ngtcp2's stateless Retry boundary. Unknown
  Initial packets receive a token bound to the remote endpoint, original DCID
  and Retry SCID. Invalid or expired tokens are dropped, and only a verified
  retry reaches the resource-manager admission hook and TLS allocation.
- Diagnostics expose effective limits, live reservations and typed rejection
  counters, but are never read as control state.

## Rejected Patterns

- Plugin-owned bootstrap workers or discovery refresh during startup.
- Copying local capabilities into a remote authenticated session.
- Manual acquire/release pairs whose exception and cancellation paths are not
  owned by one move-only value.
- Releasing queued-byte credit when a write coroutine merely handed bytes to a
  native transport queue.
- Closing an authenticated connection solely because Identify failed.

## Evidence

- Raw lifecycle: optional/strict bootstrap, shared startup budget, dynamic
  removal and disconnected maintenance.
- Identify: outbound/inbound session activation, full protocol replacement,
  partial Push preservation, multipart Go framing, equal-sequence Rust refresh,
  verified signed-record storage, unknown-on-failure and connection usability
  after failure.
- Resources: scoped session/dial/stream/relay counters, reason-specific denials,
  Yamux blocked-window lifetime, interval-aware QUIC ACK lifetime and Circuit
  Relay deadline/per-direction byte ownership.
- QUIC public connection and stream operations are exercised from independent
  caller strands while native state remains serialized on the transport owner
  strand. The full QUIC and P2P suites cover connect, accept, stream I/O, close
  and listener shutdown through that boundary.
- Plugin: the same `node::async_start()`/`async_stop()` path with Secrets and
  ObjectDB setup before listeners.
- The focused live runner on the reviewed tree produced 83 artifacts with no
  non-`ok` result. Forge
  accepted Go's canonical signed PeerRecord and Rust's legacy signed PeerRecord
  over QUIC, TCP Noise and TCP TLS while learning the donor protocol snapshots.
  Go accepted Forge's canonical signed PeerRecord and reported it through the
  Identify-completed event used by current Go libp2p; that donor intentionally
  does not write the received record back into its peer store.
- The pinned Rust Identify decoder does not accept the canonical Go/Forge
  envelope. Forge records that donor limitation instead of weakening its own
  canonical output, and still accepts Rust's exact legacy profile on input.
- The ten-node mixed GossipSub mesh (four Forge, three Go and three Rust nodes)
  delivered all three implementation-authored messages exactly once to every
  listener. Local codec coverage alone is not a production claim.
