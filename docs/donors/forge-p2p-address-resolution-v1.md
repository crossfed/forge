# P2P Address Resolution And Dial Ownership

## Donor Sources Inspected

- Go libp2p `9cfe2cc00be5b20a0be737f002c99f81b92255c5`:
  `p2p/net/swarm/dial_worker.go`, address expansion, delayed address ranking,
  already-completed attempts, in-flight accounting and TCP upgrade progress.
- Rust libp2p `22fb4c784fc55ad8b15d05fdc9f98d663107d4cb`:
  `swarm/src/connection/pool.rs`, bounded pending connections, concurrent dial
  errors reported with the successful connection, peer-ID checking and native
  close on failed admission.

## Accepted Mechanics

One logical peer dial owns bounded concrete attempts. Address ranking is not a
new protocol: authenticated TCP/QUIC and multistream negotiation keep their
existing wire formats. Go-style TCP progress can delay competing launches;
Rust-style concurrent errors remain visible even when a later attempt succeeds.

Forge separates scheduler-owned unpublished attempts from node-owned session
publication. The scheduler drains native losers before returning its winner.
The node retains one logical dial reservation through admission or rollback,
and closes the exact failed session rather than all sessions for that peer.

DNS roots are retained separately from temporary concrete addresses. Cancellation
of a loser or expiration of the whole request does not prove that its source
root is unreachable. Root failure requires completed attributable failures for
every eligible planned child; unlaunched siblings keep the root neutral.

## Evidence Boundary

Scheduler regressions cover attempt caps, TCP-only selection, inferred peer
admission, cancellation and source-root attribution. Node regressions exercise
real TCP connections through `/dns4/localhost`, peer/address gater stages,
single logical dial reservations and stop cleanup.

Rendezvous and peer-exchange hidden-peer fixtures use a shared, joined UDP DNS
server for `hidden.test`, configured through the normal node resolver options.
They do not depend on macOS hostname resolution or relax third-party loopback
filtering. The AutoNAT regression uses distinct certificate identities with
insecure mode disabled, rather than unrelated explicit peer IDs.

`node_session_tests.cpp` holds the real session-admission gate while canceling
or expiring the commit operation. Native close is independently blocked: the
operation must retain transport/resource ownership until close drains, without
publishing a session. Disabling the deadline-to-worker cancellation bridge
makes both regressions fail before cleanup starts.

The cached-session regression selects an owned continuation, fully retires the
selected connection, then runs the continuation against a real TCP peer with
an exhausted inbound stream budget. A fresh authenticated connection succeeds
before stream reset; only one fresh dial is allowed. Replacing the owned cached
pointer with another registry lookup produces two fresh handshakes and fails
the regression. The three fixtures pass 79 assertions; these deliberate RED
mutations are not retained in production sources.

These checks are not a substitute for live Go/Rust DNS and dual-stack interop.
The focused DNSADDR smoke covers eight Forge/Go/Rust directions over native
TCP/Yamux and private TCP/PNET/Yamux. Its authoritative UDP server records both
TXT hops; the checker correlates those records with the launched DNS root and
the authenticated echo peer. Rust records the completed TCP security/muxer
upgrade below the DNS wrapper rather than interpreting the outer DNS endpoint
as a numeric transport address. Missing/mismatched PNET controls have separate
artifact directories for every scenario. All eight focused smoke directions
pass, but these noncanonical runs do not close the exact-head delivery gate.

### Recovery And Shutdown Checkpoint

The dialing and scheduler suites exercise the full failure, suppression,
successful probe and reset cycle independently for UDP and IPv6 TCP. Their
51 cases pass 840 assertions, including real resource reservations around
scripted transport attempts. This is component evidence, not a public-network
black-hole experiment.

`node_addressing_tests.cpp` uses authoritative A and AAAA answers with a real
IPv6 TCP peer that accepts but never answers the handshake. A real IPv4 node
wins in both native and private profiles. The test checks one logical dial,
resource release immediately after connect returns, subsequent peer-observed
IPv6 close, authenticated echo and source-root attribution. It passes 88
assertions without a production bypass or a silent IPv6 skip.

The canonical run on `606421a9` produced 124 records and six reported failures.
All eight DNSADDR directions have successful exchanges and graceful listener
termination. Four provider fixtures used insufficient self-provider evidence;
the Forge provider fixture incorrectly treated an incoming-request counter as
outgoing-query proof. Rust-to-Go relay also exposed a loopback reservation
address-filter mismatch in the fixture configuration. Those failures remain
delivery blockers, not accepted limitations.

Review additionally found 14 Forge QUIC listener records with forced SIGTERM
termination that the runner had not added to its failures. They must not be
counted as successful cases. A Go-to-Forge Ping reproduced the shutdown stall:
a canceled outbound QUIC connect attempted an interruptible cleanup await and
never released its native-lifetime teardown ticket. Cleanup now clears the old
cancellation slot before resetting cancellation, terminalizes on the native
strand, joins background work and retains the opaque owner through that join.
Early native failures use the same cleanup path.

`quic_inherited_connect_cancellation_releases_native_lifetime_after_initial`
fails on the old implementation while the silent UDP peer and runtime remain
alive, and passes after the fix. The native-handshake terminal-winner test
preserves the existing atomic cancellation/timeout arbitration; restoring the
incorrect inherited-cancellation OR makes the committed-timeout case fail.
This tests committed state ordering, not wall-clock signal-arrival ordering.
The two QUIC regressions pass 20 assertions. Focused Go/Rust Ping and Identify
checks also end with graceful listener shutdown after the fix. Temporary
diagnostic instrumentation and deliberate RED mutations are not retained.

The full 709-case P2P suite, DNS, structure and QUIC/P2P package checks passed
with the cleanup fix; the additional terminal-winner case was checked separately.
Before delivery, the runner must fail on forced cleanup while retaining primary
and cleanup evidence, provider fixtures must prove an independent network lookup,
and the canonical matrix must be rerun on the final head. Raw lifecycle bootstrap
still accepts `endpoint` rather than a DNSADDR root; that remaining PR5 integration
gap must not be described as only a Stage 7 plugin-configuration task.

The PR remains under validation until controlled DNSADDR, transport parity,
package consumers and the exact-head donor matrix have passed. No production
capability is promoted by this source note.
