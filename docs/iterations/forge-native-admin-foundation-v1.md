# Forge Native Admin Foundation v1

## Status

This document defines the Forge work needed by native C++ backends that serve a
browser administration application and call typed downstream services. It is an
implementation plan, not a shipped API contract.

The first downstream consumer is the optional Spine Admin product. Public Forge
types, targets, modules and configuration remain product-neutral.

## Goal

Provide reusable browser authentication and HTTP publication mechanics without
creating a frontend framework or another remote-service client layer:

```text
browser
   |
   | same-origin HTTPS, owner session and CSRF protection
   v
native product backend
   |-- product workflow FORGE_HTTP_API
   |-- prebuilt TypeScript assets
   |-- forge.auth pairing and session state machines
   `-- existing typed Forge clients
```

The native backend remains the browser authority boundary. The browser does not
receive downstream service credentials, private signing keys or an unrestricted
proxy to every downstream API.

## Dependency Decision

No new external C++ dependency is required for the owner-only v1 design.

Forge already provides the required foundations:

| Need | Existing owner |
| --- | --- |
| Async runtime and cancellation | `forge_app`, `forge_asio`, Boost.Asio |
| HTTP server, router and files | `forge_net_http`, Boost.Beast, Boost.URL |
| Typed API and OpenAPI | `forge_api_core`, `forge_api_http` |
| HTTP lifecycle and middleware | `forge_plugins_http_server` |
| Cryptographic random | `forge_crypto_core`, backed by OpenSSL `RAND_bytes` |
| Token digests | `forge_crypto_digest` |
| Base64URL | `forge_codec_base64` |
| JSON | `forge_codec_json`, backed privately by Glaze |
| Typed failures | `forge_exceptions` |
| Config and secret delivery | Forge Config and Crypto Secrets |
| Product persistence | Forge DB Object and MDBX through the DB Store plugin |
| Structured operational events | `forge_log` and optional OTLP adapters |

Boost supplies transport and application mechanics; it is not used as a
substitute for cryptographic random, constant-time comparison or durable
transactions. Those operations stay behind existing Forge crypto and DB
boundaries.

OIDC, JOSE/JWT, JWKS, enterprise identity, password authentication and RBAC are
outside v1. A future multi-user block must select a mature implementation after
a separate donor and security review. JWT or OIDC must not be implemented
manually with Boost.JSON and OpenSSL primitives.

## Library Ownership

`forge.auth` becomes an empty family root. It has no aggregate target or module.
The implementation introduces three independent leaf libraries.

### `forge_auth_pairing`

```text
target:     forge_auth_pairing
namespace:  forge::auth::pairing
modules:    forge.auth.pairing.*
```

Owns product-neutral first-owner and device pairing records and transitions:

- high-entropy, short-lived bootstrap tokens;
- digest-only persisted token representation;
- bounded pending requests and expiry;
- explicit approval and rejection;
- superseding a request when identity or requested scopes change;
- one-time token consumption before reporting success;
- scope baselines, down-scope rotation and no implicit escalation;
- credential rotation and revocation;
- typed replay, expiry, capacity and state-transition failures.

It does not own HTTP, cookies, UI, CLI, a database driver, product roles or
automatic approval policy.

### `forge_auth_session`

```text
target:     forge_auth_session
namespace:  forge::auth::session
modules:    forge.auth.session.*
```

Owns opaque browser-session records and transitions:

- random session and CSRF material;
- digest-only persisted secrets;
- absolute and idle expiry;
- renewal, rotation, revocation and logout;
- session fixation resistance when pairing becomes an owner session;
- device identity and approved-scope binding;
- bounded validation and typed failure categories.

It does not own browser headers, persistence, product permissions or chain
signing policy. A browser session proves access to product workflows; it is not
a transaction signature or a downstream service credential.

### `forge_auth_http`

```text
target:     forge_auth_http
namespace:  forge::auth::http
modules:    forge.auth.http.*
```

Adapts pairing/session decisions to neutral HTTP security mechanics:

- strict Cookie parsing and serialization;
- `Secure`, `HttpOnly`, `SameSite`, `Path`, expiry and `__Host-` invariants;
- exact Origin allow-list checks;
- CSRF cookie/header verification for state-changing methods;
- session extraction and response cookie construction;
- browser security-header policy;
- redacted typed authentication failures;
- hooks for product-owned rate limiting and audit events.

The library depends on `forge_auth_session` and `forge_net_http`. It does not
depend on `forge_plugins_http_server`: libraries must not import product or
plugin runtime APIs. A product constructs a server-plugin middleware descriptor
around these neutral functions.

## Existing Forge Changes

### `forge_crypto_core`

Add one public constant-time byte comparison primitive backed by the existing
OpenSSL boundary. Migrate the private implementation in HTTP Bearer
authentication to the shared primitive.

Authentication code compares fixed-size token digests, not clear variable-size
tokens. Length and malformed-input checks happen before the digest comparison.

No new random implementation is needed. Pairing and session tokens use the
existing `forge::crypto::core::random_bytes` or `random_array` APIs.

### `forge_net_http`

Add focused modules inside the existing target:

```text
forge.net.http.cookie
forge.net.http.assets
```

`cookie` owns RFC-constrained parsing and serialization mechanics. Auth policy
such as which cookie names or scopes are allowed remains in `forge_auth_http`.

`assets` maps an installed, prebuilt frontend bundle to read-only HTTP
responses. It must provide:

- GET and HEAD only;
- canonical path resolution below one configured root;
- rejection of `..`, encoded traversal, NUL, separator confusion and symlink
  escape;
- no directory listing;
- bounded files and headers;
- MIME selection from an allow-listed table;
- ETag and conditional response support;
- immutable caching for fingerprinted assets;
- no-cache policy for `index.html`;
- an explicit optional single-page-application fallback.

It does not build TypeScript, upload files, watch source directories, execute
Node.js or become a general web framework.

### `forge_plugins_http_server`

Extend the local-only server API with a constrained asset publication operation:

```cpp
publish_assets(asset_publication value);
```

`publish_assets` means mount an already built directory such as
`share/product/web` at a configured URL prefix before server startup. It is not
an upload endpoint and does not expose the raw router to product plugins.

The publication record contains only neutral mechanics such as mount path,
filesystem root, index file, SPA fallback and cache limits. Product names,
frontend manifests and authorization policy remain downstream-owned.

Middleware responses also gain an append operation or a typed cookie operation
that preserves repeated `Set-Cookie` fields. HTTP requires one header field per
cookie; multiple cookies must not be comma-combined. A successful session flow
may legitimately return, for example:

```text
Set-Cookie: __Host-owner-session=...; Secure; HttpOnly; SameSite=Strict; Path=/
Set-Cookie: __Host-owner-csrf=...; Secure; SameSite=Strict; Path=/
```

The first cookie carries the opaque session credential and is not readable by
JavaScript. The second supports the CSRF double-submit/header check and may be
readable by the same-origin application. A short-lived pre-session pairing
cookie can produce another independent `Set-Cookie` field. The current
middleware `set_header` replacement semantics cannot represent this correctly,
although the lower `forge_net_http` response already preserves repeated cookie
fields.

The plugin continues to reject arbitrary raw-route publication. Typed API
publication, middleware and constrained asset publication remain the only
supported contribution surfaces.

## Explicitly Unchanged Components

The following libraries are consumers or existing foundations and do not need
an auth-specific API change:

- `forge_api_core` and `forge_api_http`;
- Forge Chain API contracts, raw client, verified client and submission client;
- Forge chain transaction builder and ABI codec;
- Forge keystore and signer providers;
- Forge DB Core, ObjectDB, MDBX and the DB Store plugin;
- Forge Config, App, Asio, Log and OTLP.

If a downstream node-management operation is absent from its existing typed
admin API, that operation is designed separately in the owning API contract. It
must not be smuggled into browser authentication or static-asset libraries.

## Persistence Boundary

Pairing and session libraries own records and valid transitions, not a physical
database. The consuming backend owns persisted models and executes transitions
inside its transaction boundary.

The first Spine Admin implementation uses a separate named MDBX/ObjectDB store,
for example `admin`, with neither Revision nor BlobDB unless a concrete model
requires them. It must never reuse the chain `state` or `block` stores.

Atomic product operations include:

- consume bootstrap token and create pending request;
- approve pending request and create owner credential;
- replace pre-session state with an authenticated session;
- rotate or revoke a credential and invalidate its sessions;
- persist the matching security audit record.

This preserves reusable Forge auth logic while keeping product model IDs,
operator roles, retention and audit vocabulary out of Forge.

## Donor Baseline

The pairing behavior is based on the local OpenClaw checkout:

```text
repository: https://github.com/openclaw/openclaw.git
commit:     d6367c2c55a2ef40f189300862d022b0276cc017
```

Reviewed paths include:

- `src/pairing/setup-code.ts`;
- `src/infra/device-bootstrap.ts`;
- `src/infra/device-pairing.ts`;
- `src/infra/pairing-token.ts`;
- `src/infra/pairing-files.ts`;
- `src/infra/json-files.ts`;
- `src/gateway/device-auth.ts`;
- `src/gateway/server/ws-connection/message-handler.ts`;
- `src/gateway/server/ws-connection/handshake-auth-helpers.ts`;
- `src/gateway/server.auth.browser-hardening.test.ts`;
- `ui/src/ui/device-identity.ts`;
- `ui/src/ui/device-auth.ts`;
- `docs/channels/pairing.md`.

Accepted patterns are short-lived bootstrap material, one-time consumption,
bounded pending requests, explicit local approval, supersede on identity or
scope changes, constant-time secret checks, rotation/revocation, atomic private
storage, Origin checks and failed-auth rate limiting.

OpenClaw's long-lived browser device key and token in `localStorage` are not
accepted for this architecture. The native backend owns credentials and gives
the browser an opaque Secure HttpOnly session cookie.

## Security Invariants

- Clear bootstrap/session tokens never enter logs, diagnostics, config output
  or persisted records.
- Browser responses never contain downstream service credentials or private
  signing keys.
- Pairing approval is unavailable through the public browser API.
- A consumed bootstrap token cannot create or modify a second request.
- Scope changes require an explicit transition and cannot silently escalate.
- Session issuance rotates the pre-session identifier.
- State-changing browser methods require an approved session, exact Origin and
  valid CSRF evidence.
- Session, CSRF and pre-session cookies use distinct names and secrets.
- Production browser access requires HTTPS. Plain HTTP is restricted to an
  explicit loopback development policy.
- Assets and API share one origin, but asset paths never bypass API middleware
  or expose arbitrary filesystem files.
- Product rate limits are applied to issue, begin, complete and authentication
  failures before expensive or state-changing work.

## Delivery Order

1. Add `forge.crypto.core` constant-time comparison and migrate Bearer auth.
2. Add `forge_auth_pairing` with deterministic transition and adversarial
   tests.
3. Add `forge_auth_session` with fixation, expiry, rotation and revocation
   tests.
4. Add `forge.net.http.cookie` and repeated `Set-Cookie` preservation.
5. Add `forge_auth_http` and middleware integration tests.
6. Add `forge.net.http.assets` and constrained server-plugin publication.
7. Add package components, relocation consumers, READMEs and donor
   traceability.
8. Integrate the released components into a downstream native admin backend.

Each new library follows the current `create-library` skill. The HTTP server
plugin extension additionally follows `create-plugin`. Aggregate targets,
forwarding modules and compatibility aliases are not introduced.

## Acceptance

Crypto and auth:

- random token uniqueness and fixed entropy requirements;
- constant-time digest comparison used by Bearer, pairing and sessions;
- expiry boundaries and injected-clock tests;
- token replay, concurrent consume, stale approval and superseded request;
- scope escalation rejection, rotation, revocation and session invalidation;
- malformed, oversized and non-canonical token rejection;
- typed Forge exceptions at every public boundary.

HTTP:

- strict cookie parse/format round trips;
- cookie attribute injection and header-splitting rejection;
- multiple `Set-Cookie` fields preserved through middleware and a live server;
- session, CSRF, exact Origin and security-header behavior;
- no CORS wildcard with credentialed browser sessions;
- cancellation, deadlines, body/header limits and failed-auth rate limits.

Assets:

- GET/HEAD, ETag, not-modified and cache policy;
- missing files and SPA fallback;
- traversal, percent-encoding, symlink escape and directory-listing rejection;
- MIME allow-list and response-size limits;
- production package relocation with an installed frontend bundle.

Integration:

- one typed workflow API and asset bundle on the same origin;
- first-owner pairing through a private local approval boundary;
- restart with durable pending/session state;
- credential rotation/revocation without restarting the HTTP server;
- downstream typed client remains private to the backend;
- full local CTest, structure/format gates and `git diff --check`.

## Out Of Scope

- A `Forge.Web` frontend framework.
- React, Vue, Vite or any TypeScript build ownership in Forge.
- Product UI, workflow DTOs or node-management policy.
- Transparent proxying of downstream APIs.
- OIDC, JOSE/JWT, JWKS, SSO, multi-user RBAC or password login.
- Browser-held private signing keys or long-lived downstream credentials.
- A new HTTP transport, Chain API client, database engine or crypto vendor.
