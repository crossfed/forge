# Forge Auth AppAuth

`forge_auth_appauth` is the macOS 12+ native-desktop backend for
`forge_auth_oauth2`. It pins the Apple AppAuth SDK to `3.0.0` and delegates
Authorization Code + PKCE to that SDK; Forge does not reproduce an OAuth state
machine.

## Module

- `forge.auth.appauth.provider`: macOS AppAuth provider, configuration and
  typed backend failures.

```cpp
import forge.auth.appauth.provider;

auto provider = forge::auth::appauth::provider::create({
    .issuer = "https://issuer.example",
    .client_id = "native-desktop-client",
    .scopes = {"openid", "profile"},
    .keychain_service = "org.example.forge",
    .keychain_account = "desktop",
});

co_await provider->async_authorize({.scopes = {"openid", "profile"}});
auto token = co_await provider->async_access_token({.scopes = {"openid", "profile"}});
```

`async_authorize` uses issuer discovery, an AppAuth-generated state and nonce,
PKCE S256, the default system browser and an AppAuth loopback redirect handler.
The private AppAuth `OIDAuthState`, including refresh state, is stored only as
a secure coded archive in the macOS Keychain. The public API exposes neither
that state nor a refresh token. Access tokens stay move-only and are not
reflected, serialized or logged.

`async_invalidate` removes the local Keychain state. AppAuth 3.0.0 has no
public token-revocation API, so `async_revoke` returns typed
`unsupported_request` rather than pretending to revoke an issuer-side grant.

This package is Apple-only and owns no application UI. Its private backend
links AppKit only to open the system browser through AppAuth. The leaf CMake
requires a coordinator-provided `AppAuth::AppAuth` target with
`AppAuth_VERSION=3.0.0`; its named-module implementation remains C++, while
Objective-C++, AppAuth and Keychain code are isolated in the private
`appauth_backend.mm` bridge. The target must expose AppAuth's macOS
external-user agent and loopback-handler headers.

## Tests

Executable registration belongs to central `tests/`. Integration must add a
macOS conformance target covering Keychain round-trip/replacement, absence of
stored state as typed `setup_required`, discovery failure redaction, request
constraint matching, a loopback redirect and PKCE S256/state/nonce evidence
against a local OIDC test issuer.

This leaf is not an IdP, JWT validator, browser UI or general Keychain API.
