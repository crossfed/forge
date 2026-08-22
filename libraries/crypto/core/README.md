# Forge Crypto Core

`forge_crypto_core` owns the low-level memory primitives shared by Crypto
libraries. Package component: `crypto_core`. Public namespace:
`forge::crypto::core`.

## Modules

- `forge.crypto.core.types`: owned byte containers.
- `forge.crypto.core.secret_bytes`: move-only secret storage with secure erase.
- `forge.crypto.core.secret_string`: copyable secret text with explicit scrubbed
  copy/move assignment and destruction.
- `forge.crypto.core.random`: cryptographically secure random bytes.
- `forge.crypto.core.constant_time`: constant-time equality for equal-length
  byte sequences at authentication boundaries.

```cpp
import forge.crypto.core.random;
import forge.crypto.core.secret_bytes;

auto secret = forge::crypto::core::secret_bytes{
   forge::crypto::core::random_bytes(32)};
```

The target depends on `forge_exceptions` and OpenSSL Crypto. It does not own
hash, cipher, key or certificate algorithms. `test_forge_crypto_core` covers
secret lifetime, random byte and constant-time comparison contracts; the
package test verifies installed module consumption.
