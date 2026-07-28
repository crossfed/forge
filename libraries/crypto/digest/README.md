# Forge Crypto Digest

`forge_crypto_digest` owns deterministic digest algorithms, HMAC and Raw pack
hashing. Package component: `crypto_digest`. Public namespace:
`forge::crypto::digest`.

## Modules

The leaf exports SHA-1, SHA-224, SHA-256, SHA-3, SHA-512, RIPEMD-160, BLAKE2,
`forge.crypto.digest.hmac` and `forge.crypto.digest.packhash`.

```cpp
#include <string>

import forge.crypto.digest.sha256;

const auto value = forge::crypto::digest::sha256::hash(
   std::string{"canonical bytes"});
```

`data()` and `to_uint8_span()` return borrowed views. Clang lifetime analysis
tracks them back to the owning digest, so a view cannot be retained after a
temporary digest is destroyed. Name the digest before retaining its view;
passing a temporary view directly to a non-retaining operation remains valid.

Dependencies are `forge_crypto_core`, Codec Hex, Raw, Variant, Forge Core and
OpenSSL Crypto. Digest values preserve their existing Raw and Variant layouts.
The library does not own text transport profiles or signing policy.
`test_forge_crypto_digest` covers golden vectors, incremental encoders, HMAC,
BLAKE2, serialization and compiler-enforced borrowed-view lifetimes.
