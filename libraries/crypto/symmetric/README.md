# Forge Crypto Symmetric

`forge_crypto_symmetric` owns authenticated and conventional symmetric
encryption plus key derivation. Package component: `crypto_symmetric`. Public
namespace: `forge::crypto::symmetric`.

## Modules

- `forge.crypto.symmetric.aes`
- `forge.crypto.symmetric.chacha20_poly1305`
- `forge.crypto.symmetric.kdf`

```cpp
import forge.crypto.symmetric.aes;

const auto key = forge::crypto::symmetric::aes::generate_aes256_key();
```

The target depends on `forge_crypto_core`, `forge_exceptions` and OpenSSL
Crypto. It does not load secrets, own configuration or provide a vault; those
are application/plugin responsibilities. `test_forge_crypto_symmetric` covers
roundtrips, authentication failures, streaming and KDF limits.
