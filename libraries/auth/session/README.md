# Forge Auth Session

`forge_auth_session` owns product-neutral browser-session records and pure
state transitions. It has no database, HTTP, cookie, plugin, route or product
role dependency.

Public modules:

- `forge.auth.session.exceptions`: typed token, expiry, credential-binding and
  state-transition failures.
- `forge.auth.session.types`: digest-only persisted records, issued secrets and
  typed validated principals.
- `forge.auth.session.session`: issuance, validation, CSRF verification, idle
  renewal, rotation, logout and revocation transitions.

Issuance and rotation create independent 32-byte session and CSRF secrets as
canonical unpadded base64url `forge::crypto::core::secret_string` values. The
persisted `session_record` holds only SHA-256 digests plus a pairing credential
ID, generation, identity and canonical scopes. No API accepts caller-selected
new secret material.

Callers provide trusted, non-decreasing wall-clock `now` values. Records retain
these timestamps across restart and reject transitions earlier than persisted
creation or activity times. Absolute and idle expiry are explicit; idle renewal
never extends beyond absolute expiry.

Dependencies: `forge_auth_pairing`, `forge_codec_base64`, `forge_crypto_core`,
`forge_crypto_digest`, `forge_exceptions`.
