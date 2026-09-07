# Forge Crypto Symmetric

`forge_crypto_symmetric` owns authenticated and conventional symmetric
encryption plus key derivation. Package component: `crypto_symmetric`. Public
namespace: `forge::crypto::symmetric`.

## Modules

- `forge.crypto.symmetric.aes`
- `forge.crypto.symmetric.chacha20_poly1305`
- `forge.crypto.symmetric.kdf`
- `forge.crypto.symmetric.xsalsa20`

```cpp
import forge.crypto.symmetric.aes;

const auto key = forge::crypto::symmetric::aes::generate_aes256_key();
```

`forge.crypto.symmetric.xsalsa20` is a stream-cipher boundary backed by a
private, unmodified libsodium 1.0.22 subset. `xsalsa20::key` owns its 32-byte
secret through `forge.crypto.core.secret_bytes`; `xsalsa20::nonce` is exactly 24
bytes. `xsalsa20::stream` transforms supplied mutable spans in place and retains
partial 64-byte blocks across calls, so callers can use separate instances for
independent read and write directions. Arbitrary counter positioning is not
exposed: a stream starts at counter zero and owns its complete counter range.
Every independent stream or direction under a key requires a unique nonce.
Never reuse the same key and nonce pair; separate read and write streams under
one key must use distinct nonces.

XSalsa20 provides no authentication. A caller must pair it with a suitable
authentication boundary when active modification is in scope. The public module
does not expose libsodium headers, types or functions.

The target depends on `forge_crypto_core`, `forge_exceptions`, OpenSSL Crypto
and the private vendored XSalsa20 object target. It does not load secrets, own
configuration or provide a vault; those are application/plugin responsibilities.
`test_forge_crypto_symmetric` covers roundtrips, authentication failures,
streaming, XSalsa20 vectors/counter behavior and KDF limits.
