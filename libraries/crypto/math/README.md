# Forge Crypto Math

`forge_crypto_math` owns arbitrary-precision integer and modular exponentiation
mechanics. Package component: `crypto_math`. Public namespace:
`forge::crypto::math`.

## Modules

- `forge.crypto.math.bigint`
- `forge.crypto.math.modular_arithmetic`

```cpp
import forge.crypto.math.bigint;

const auto value = forge::crypto::math::bigint{42};
```

The target depends on Crypto Core, Codec Base64, Core, Exceptions, Variant,
OpenSSL Crypto and GMP. Ordinary asymmetric consumers do not depend on this
leaf. `test_forge_crypto_math` covers modular arithmetic vectors, failures and
big-integer behavior.
