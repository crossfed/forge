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

Deterministic integration coverage for cancellation while the session-admission
gate is occupied and for cached-session removal between selection and stream
open remains pending. Component cancellation tests and source review do not
stand in for those race fixtures.

These checks are not a substitute for live Go/Rust DNS and dual-stack interop.
The PR remains under validation until controlled DNSADDR, transport parity,
package consumers and the exact-head donor matrix have passed. No production
capability is promoted by this source note.
