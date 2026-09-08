# Forge Auth OIDC Agent

`forge_auth_oidc_agent` is a bounded asynchronous adapter for the public
`liboidc-agent5` C API pinned to oidc-agent `5.3.8`. It calls only
`getAgentTokenResponse`, `getAgentTokenResponseForIssuer` and
`secFreeAgentResponse`; no IPC protocol or refresh-token persistence is
reimplemented in Forge.

## Modules

- `forge.auth.oidc_agent.exceptions`: typed adapter and C-response failures.
- `forge.auth.oidc_agent.client`: account/issuer selection and the concrete
  OAuth2 token provider.

```cpp
import forge.asio.compute;
import forge.auth.oidc_agent.client;

auto blocking = forge::asio::compute::pool{{
    .worker_threads = 1,
    .max_pending_tasks = 0,
    .max_waiting_submissions = 32,
    .thread_name = "forge-oidc-agent",
}};
auto provider = forge::auth::oidc_agent::client::create(
    {
        .selection = {.kind = forge::auth::oidc_agent::selection_kind::issuer,
                      .value = "https://issuer.example"},
        .application_hint = "forge-desktop",
    },
    blocking.get_executor());
auto token = co_await provider->async_access_token({.minimum_validity = std::chrono::seconds{60}});
```

The embedding application owns the bounded blocking pool and passes its
executor to every client that shares the same `oidc-agent` lane. Use one worker
because the public C API is blocking and exposes no hard-cancel operation.
Cancellation stops the caller's wait; a late result is securely released and
never delivered. Application shutdown must stop admission and coordinate agent
availability before joining the pool, because an in-flight external C call may
delay that join.
The private C response is freed with `secFreeAgentResponse` on every path; its
token is copied into a move-only OAuth2 `access_token` before that release.
The agent owns account configuration and refresh state. An agent error becomes
typed `forge::auth::oauth2::exceptions::setup_required`; its error/help text is
not logged or put into Forge exception context.

The leaf CMake requires a coordinator-provided `OIDCAgent::client` target with
`OIDCAgent_VERSION=5.3.8`, public header `oidc-agent/api.h`, and all
transitive dependencies required by liboidc-agent5 (such as libsodium where
applicable).

## Tests

The focused adapter test uses the public oidc-agent C ABI with a deterministic
test implementation. It covers account and issuer selectors,
scope/audience/minimum-validity mapping, success/error response release,
malformed response rejection, typed setup-required errors, and cancellation
while the blocking C call remains in flight. Tokens never appear in test output.
A live agent/account remains an environment-specific interoperability gate.

This leaf is not an OIDC agent process manager, account-config generator,
browser flow, issuer validator or refresh-token store.
