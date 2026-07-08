# Forge API QUIC

`forge_api_quic` is the typed API binding adapter for QUIC streams.

It owns only API frame binding behavior:

- public modules live under `forge.api.quic.*`;
- public namespace is `forge::api::quic`;
- QUIC transport mechanics stay in `forge_net_quic` / `forge::net::quic`;
- generic frame serving stays in `forge_api_transport`.

Use this library when an API contract should be served over a QUIC stream.
