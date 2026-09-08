# Chain Signer Plugin

`forge::plugins::chain::signer` composes injected local signing providers into
the Chain transaction and finality signer contracts. It owns no private keys,
transport listener, HTTP route, P2P publication, or finality safety state.

## Identity

- Target: `forge_plugins_chain_signer`
- Package component: `plugins_chain_signer`
- Plugin id: `forge.plugins.chain.signer`
- Transaction API id: `forge.chain.api.transaction_signer`
- Finality API id: `forge.chain.api.finality_signer`
- Public modules:
  - `forge.plugins.chain.signer.plugin`
  - `forge.plugins.chain.signer.descriptor`
  - `forge.plugins.chain.signer.types`
  - `forge.plugins.chain.signer.exceptions`

## Composition

The application injects named `forge::crypto::signer::provider` instances for
transactions and named `forge::crypto::bls::signer::provider` instances for
finality. `plugin_options` carries public keys, provider names, exact
transaction policies, the selected finality provider, a semantic authorization
provider, and an admission limit. It never carries private key material or a
transport credential.

Every transaction action authorization must match one policy exactly on chain,
action contract/name, authorization actor/permission, and the server-supplied
caller source/fingerprint. Context-free actions, empty authorization lists,
missing rules, ambiguous provider selection, and cross-provider transactions
are denied. There are no wildcard rules and an empty policy set signs nothing.

The envelope allowlist intentionally does not interpret application ABI action
data. A remote-enabled profile (one with caller rules) therefore requires the
injected `semantic_authorization_provider`; product composition supplies this
ABI-aware authorization before any provider cryptography occurs. Local-only
profiles may omit it. `max-delay-seconds` defaults to `0`, denying deferred
transactions unless a profile explicitly opts into a bounded delay.

```cpp
import forge.plugins.chain.signer.plugin;

auto descriptor = forge::plugins::chain::signer::descriptor({
   .transaction_providers = {{
      .name = "transaction",
      .value = transaction_provider,
   }},
   .finality_providers = {{
      .name = "finality",
      .value = finality_provider,
   }},
   .semantic_authorization = semantic_authorization,
   .initial_config = {
      .transaction_profiles = {exact_policy},
      .finality = finality_binding,
      .max_inflight = 32,
      .max_queued = 128,
      .max_queued_bytes = 64U * 1024U * 1024U,
      .max_per_caller = 32,
      .shutdown_timeout_ms = 5000,
   },
   .audit = audit,
   .now = clock,
});
```

The transaction contract is remote-capable but its caller is a required
server-supplied value. Bindings must authenticate a transport principal and put
`forge::api::auth::authenticated_caller` into `trusted_invocation`; clients do
not control that value. The finality contract is local-only: `identity()`
returns typed public key and proof of possession, while `sign_vote(block_ref,
vote_kind)` returns a typed `finalizer_vote`, never raw BLS bytes or safety
state.

## Runtime

Cheap exact-profile selection runs before admission. Admission is bounded by
`max_inflight`, request count, queued decoded bytes and per-caller quota; the
queue byte budget is independent from each profile's final
`max_packed_bytes`. Caller cancellation and plugin stop propagate to active
provider operations. `request_stop()` denies new work and cancels queued and
active calls; `shutdown()` drains them within `shutdown_timeout_ms` before
providers can be released.
