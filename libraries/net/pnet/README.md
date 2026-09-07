# Forge Pnet

`forge_net_pnet` owns a product-neutral pre-shared-key stream protector. A
`pre_shared_key` decodes the canonical libp2p swarm-key base16 form, owns its
32-byte value as move-only secret material, and exposes only a domain-separated
operational fingerprint. That fingerprint is SHA-256 over
`"forge.net.pnet.operational-fingerprint.v1" || 0x00 || decoded PSK`.

`protector::async_protect` eagerly writes one local 24-byte nonce, then returns
a stream whose peer nonce is read lazily. Read and write directions use
independent raw XSalsa20 streams. The primitive adds no KDF, MAC, frame, or
protocol ID. It protects a stream but does not authenticate a peer; a wrong PSK
normally becomes visible only during the following security negotiation.

The optional stop token covers the eager nonce write. Cancellation requests the
lower transport exactly once; if the nonce write wins the terminal race, the
protected connection is returned normally.

## Interop Evidence

`tests/libp2p_interop` registers four direct Forge-to-Go/Rust and Go/Rust-to-
Forge TCP/Yamux directions using a canonical `swarm.key` source fixture outside
the runner artifact directory. Both endpoint records must confirm PNET
negotiation and the non-secret Forge operational fingerprint. Separate missing-
key and mismatched-key controls require observed listener ingress followed by
rejection before Identify or an application stream. Registration is not a passing live-run claim, and the
evidence makes no QUIC, Relay, or DCUtR claim.
