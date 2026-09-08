# Forge Auth Workload

`forge_auth_workload` adapts a projected workload assertion file to a neutral
subject-token source. It is intended for runtimes that rotate an opaque
workload token atomically at a configured path. A token-exchange consumer may
use this source, but OAuth token exchange remains outside this leaf.

## Modules

- `forge.auth.workload.exceptions`: typed file-source failures.
- `forge.auth.workload.subject_token`: move-only opaque subject token.
- `forge.auth.workload.subject_token_source`: asynchronous subject-token source.
- `forge.auth.workload.projected_token_file`: bounded, no-symlink projected
  token-file source.

```cpp
import forge.auth.workload.projected_token_file;

auto source = forge::auth::workload::projected_token_file{
    token_read_executor,
    {.path = "/var/run/secrets/workload/token"},
};
auto assertion = co_await source.async_subject_token();
```

Each request opens the path again with `O_NOFOLLOW`, accepts only a regular
file, bounds both configured size and read retries, and compares the opened
file before and after reading. This makes normal atomic file replacement
visible without caching stale token material. It never follows a symlink,
trims bytes, parses claims or validates a JWT.

The source does not parse claims, identify an issuer, infer an audience, prove
scopes or exchange the assertion. Those decisions belong to the consuming
protocol backend.

Dependencies: `forge_asio`, `forge_crypto_core` and `forge_exceptions`.

## Tests

The central `tests/` target owns executable test registration. Integration
must add unit coverage for symlink rejection, non-regular files, size bounds,
in-place mutation/replacement retries and opaque-byte preservation.

This leaf is not a Kubernetes client, JWT verifier, token exchanger, OAuth
provider or credential store. It never logs, reflects or serializes the token.
