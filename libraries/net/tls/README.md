# forge_net_tls

`forge_net_tls` owns reusable OpenSSL context construction for Forge network
leaves. It does not own TCP listeners, HTTP routing, P2P identity, secret
delivery or application authorization.

## Public Modules

- `forge.net.tls.options` - TLS endpoint role, protocol and verification
  configuration.
- `forge.net.tls.context` - immutable TLS context snapshots and an atomically
  replaceable provider, retaining native-stream factory and TLS session helpers.
- `forge.net.tls.exceptions` - typed Forge TLS configuration and peer-verification
  failures.

Target: `forge_net_tls`.

## Security Boundary

New contexts use TLS 1.3 only unless `protocol_policy::system_default` is
selected explicitly. PEM material is bounded before OpenSSL parses it. Peer
verification requires either configured trust anchors or default verification
paths. `require_peer_certificate` is server-only and requires a verifiable trust
path; a supplied client chain is verified by OpenSSL rather than accepted through
a permissive callback. Product mutual-TLS policy may require explicit anchors
above this neutral leaf.

Client SNI and ALPN setup, server ALPN selection, peer-chain extraction, hostname
checks, fingerprint checks and custom peer validation belong to this leaf. Server
selection preserves client preference and returns OpenSSL `NOACK` for malformed
or unmatched offers.

`context_provider::replace()` builds and validates the next immutable snapshot
before publishing it atomically. New connections acquire a new snapshot; streams
returned by `make_asio_stream()` and `make_beast_stream()` retain their original snapshot,
so credential rotation cannot invalidate an established session. The mutable
OpenSSL context is intentionally not public.

## Dependencies

- `forge_exceptions`
- `forge_crypto_pki`
- Boost.Asio SSL and Boost.Beast
- OpenSSL
