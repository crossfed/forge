# P2P Node Plugin

`forge::plugins::p2p::node` owns one shared `forge_net_p2p` node and exposes typed
contribution APIs for protocol handlers and API-over-P2P publication.

## Current Support State

The plugin now provides the Stage 2 production foundation: identity material is
loaded through Crypto Secrets, peer/provider/Rendezvous state is persisted in a
dedicated DB Store Object layer, hydration completes before listeners open and
the low-level node owns bounded Kademlia routing state. It remains a
bootstrap/static-topology adapter until Stage 3 moves bootstrap and topology
maintenance into the node and completes Identify lifecycle ownership.

Insecure memory mode remains an explicit local-test path only. Current support
classifications live
in
[`p2p_feature_inventory.json`](../../../tests/libp2p_interop/p2p_feature_inventory.json);
isolated codec and interop fixtures do not promote this plugin to production.

## When To Use

- A Forge application needs one shared P2P node managed by `forge_app`.
- Product plugins need to publish protocol handlers or typed APIs over the same
  peer/session substrate.
- Diagnostics, resolver and pubsub plugins should compose over one node instead
  of creating parallel network stacks.

## When Not To Use

- Do not create product routing, durable queue or authorization policy in this
  plugin.
- Do not instantiate raw `forge::net::p2p::node` separately inside application
  plugins.
- Do not enable insecure test mode outside local tests.

## Identity

- Target: `forge_plugins_p2p_node`
- Package component: `plugins_p2p_node`
- Plugin id: `forge.plugins.p2p.node`
- Main API id: `forge.plugins.p2p.node`
- Extra API ids:
  - `forge.plugins.p2p.node.diagnostics_source`
  - `forge.plugins.p2p.node.pubsub_source`
- Config section: `plugins.p2p.node`
- Public modules:
  - `forge.plugins.p2p.node.plugin`
  - `forge.plugins.p2p.node.api`
  - `forge.plugins.p2p.node.types`
  - `forge.plugins.p2p.node.exceptions`

## What It Provides Today

- Starts and stops a shared P2P node through the `forge_app` lifecycle.
- Registers and validates the private peer-state ObjectDB schema during
  `after_initialize()`.
- Loads certificate/private-key secrets, opens ObjectDB persistence and performs
  bounded hydration before opening any listener.
- Maps config into listen/bootstrap/advertised endpoints and relay/path policy.
- Maintains configured bootstrap sessions with bounded reconnect backoff and
  protects them from connection-manager pruning.
- Lets application plugins publish typed APIs over a P2P protocol id.
- Opens typed remote API handles to peers through `remote<Interface>()`.
- Provides internal source APIs used by focused diagnostics and pubsub plugins.

`request_stop()` synchronously closes admission and listeners and initiates
session disconnect. `shutdown()` then awaits transport teardown, peer-state
flush/close and bootstrap maintenance before releasing DB and Secrets handles.
If peer-state close fails, the stopped node and its persistence ownership are
retained so a subsequent `shutdown()` can retry deterministic close; the plugin
never drops a persistence backend that still reports pending close work.
An empty active session set is not treated as proof that STCP/Yamux cleanup has
completed.

The plugin does not implement product routing policy or durable application
queues. Core libp2p-style mechanics belong to `forge_net_p2p`; this plugin composes
that substrate for applications.

## Config

```yaml
plugins:
   p2p:
      node:
         listen: ["/ip4/0.0.0.0/udp/9443/quic-v1"]
         bootstrap: ["/dns4/bootstrap.example/udp/9443/quic-v1"]
         advertised-endpoints: ["/dns4/node.example/udp/9443/quic-v1"]
         peer-id: ""
         peer-store:
            store: "p2p-peer-state"
         identity:
            certificate-secret: "p2p/node-certificate"
            private-key-secret: "p2p/node-private-key"
         api-codec: forge.raw
         api:
            deadline-ms: 5000
            max-frame-size: 16777216
         max-inflight-per-peer: 64
         max-sessions: 1024
         max-protocol-handlers: 1024
         allow-insecure-test-mode: false
         path:
            policy: direct-preferred
         relay:
            trust: known-only
            client-enabled: true
            server-enabled: false
            public-allowed: false
            reservation-ttl-ms: 60000
            max-candidates: 4
```

The named DB Store must provide an Object layer dedicated to P2P peer state.
One authoritative schema marker versions the complete private row family, so
startup validates the format without scanning durable history. A missing marker
in nonempty storage or a version mismatch fails startup; the v1 recovery path is
to remove the peer cache and hydrate it again from configured bootstrap peers.
Secret policies must allow
`p2p.identity.certificate` and `p2p.identity.private-key` respectively.
`allow-insecure-test-mode` is for local tests only. Programmatic low-level node
construction may still provide identity material directly.

## Dependencies

- `forge_app`
- `forge_api_core`
- `forge_net_p2p`
- `forge_api_transport`
- `forge_config_core`
- `forge_schema`
- `forge_plugins_db_store`
- `forge_plugins_crypto_secrets`

## Examples

### Publish A Typed API

```cpp
import forge.api.core.binding;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.plugin;

class catalog_p2p_plugin final : public forge::app::plugin {
 public:
   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto p2p = context.apis().get<forge::plugins::p2p::node::api>(
         {.id = {"forge.plugins.p2p.node"}, .major = 1});

      auto plan = forge::api::core::binding()
         .serve(context.apis())
         .export_api<catalog_api>()
         .build();

      p2p->publish_api(std::move(plan), forge::net::p2p::protocol_id{.value = "/catalog/api/1"});
      co_return;
   }
};
```

```cpp
registry.register_plugin(forge::plugins::db::store::descriptor());
registry.register_plugin(forge::plugins::crypto::secrets::descriptor());
registry.register_plugin(forge::plugins::p2p::node::descriptor());
```

## Security And Boundaries

- The plugin owns network lifecycle and contribution mounting, not product
  authorization decisions.
- Peer identity, protocol negotiation and session mechanics stay in
  `forge_net_p2p`; application plugins use typed contribution APIs.
- Insecure test mode and generated identity material are local-test-only.
- Durable peer history supplies startup candidates, not trusted live routes;
  Kademlia server admission still requires verified Identify or a successful
  DHT exchange.

## Common Mistakes

- Creating a second node in a product plugin instead of requesting the shared
  node API.
- Treating P2P reachability as authorization.
- Publishing unbounded handlers without deadline/frame limits.

## Tests

- `test_forge_quic_p2p`
- `test_forge_yamux`
- `test_forge_stcp`
- `test_forge_plugins`
- `test_forge_package_plugins_p2p_node`
