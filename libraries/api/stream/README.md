# forge_api_stream

`forge_api_stream` is the focused API-over-stream server binding. It sits above
`forge_api_core` and `forge_net_transport`, and owns only frame serving over an
already established `forge::net::transport::stream`.

## Public Modules

- `forge.api.stream.options`
- `forge.api.stream.server`

## Boundaries

- Use this library when a protocol adapter already has a `transport::stream` and
  needs to serve a typed API binding plan.
- Use `forge_api_transport` when you need the generic transport client,
  connection or session helpers.
- Socket, QUIC, P2P and application lifecycle stay in their owning libraries.
