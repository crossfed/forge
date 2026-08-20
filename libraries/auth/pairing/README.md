# Forge Auth Pairing

`forge_auth_pairing` owns product-neutral bootstrap and device-pairing records
with deterministic state transitions. It has no database, HTTP, UI, route,
product-role or approval-policy dependency.

Public modules:

- `forge.auth.pairing.exceptions`: typed token, replay, expiry, capacity,
  state, scope and identity failures.
- `forge.auth.pairing.types`: digest-only persisted records and explicit
  transition options.
- `forge.auth.pairing.pairing`: scope canonicalization and free transitions.

`begin_bootstrap` generates 32 random bytes and returns the clear bootstrap
secret as `forge::crypto::core::secret_string`, encoded with canonical unpadded
base64url. `bootstrap_record` stores only its SHA-256 digest. `consume_bootstrap`
accepts the secret, validates it with a constant-time digest comparison, marks
the record consumed, and returns a pending request for the caller to persist in
the same transaction.

Callers supply `now`, expiry and pending capacity explicitly. `now` is a
trusted, non-decreasing wall-clock value retained in persisted records across
restart; transitions reject timestamps earlier than persisted creation or
update times. Scope sets are sorted, unique and non-empty. Approval receives a non-empty stable
`credential_id` and creates a credential with that distinct ID, human identity,
scopes and generation; later rotation can only reduce scopes, and revocation is
an explicit terminal transition.

Dependencies: `forge_codec_base64`, `forge_crypto_core`,
`forge_crypto_digest`, `forge_exceptions`.
