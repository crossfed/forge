# Forge Auth OAuth 2

`forge_auth_oauth2` provides a narrow, transport-neutral access-token boundary.
It owns a move-only `access_token`, request constraints and the asynchronous
`token_provider` / `interactive_authorizer` contracts. A provider owns its
refresh state; no refresh token, authorization code, browser redirect, PKCE
exchange or OAuth state machine is part of this library.

## Modules

- `forge.auth.oauth2.exceptions`: typed configuration, setup and token failures.
- `forge.auth.oauth2.access_token`: move-only access-token value.
- `forge.auth.oauth2.token_provider`: request records and asynchronous
  token-provider interface.
- `forge.auth.oauth2.interactive_authorizer`: explicit interactive lifecycle
  contract for authorize, revoke and invalidate operations.

```cpp
import forge.auth.oauth2.token_provider;

boost::asio::awaitable<void> use(forge::auth::oauth2::token_provider& provider) {
   auto token = co_await provider.async_access_token({
       .scopes = {"storage.read"},
       .minimum_validity = std::chrono::seconds{60},
   });
   consume_bearer(token.secret().view());
}
```

`access_token` cannot be copied. Its `secret_string` is intentionally not
described, serialized or logged by this leaf. Provider implementations must not
put token material in exception context. `exceptions::setup_required` is the
typed signal that an interactive or administrator-controlled setup step is
needed.

Dependencies: `forge_crypto_core`, `forge_exceptions` and Boost.Asio headers.

## Tests

The repository keeps executable auth tests in the central `tests/` target, so
this leaf does not create an unreachable local test target. Its integration
must add central unit coverage for move-only token ownership, typed
`setup_required`, lifecycle operations, and a provider that retains refresh
state privately.

This library is not an OAuth client, token validator, credential store or HTTP
transport.
