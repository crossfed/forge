# Forge API WebSocket

`forge_api_websocket` is the typed API binding adapter for WebSocket messages.

It owns only API frame binding behavior:

- public modules live under `forge.api.websocket.*`;
- public namespace is `forge::api::websocket`;
- WebSocket connection/client mechanics stay in `forge_net_websocket` / `forge::net::websocket`;
- generic API descriptors, frames and dispatch stay in `forge_api_core`.

Use this library when an API contract should be served over a WebSocket connection.
