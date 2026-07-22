# Forge Crypto BN256

`forge_crypto_bn256` owns BN254 point addition, scalar multiplication and
pairing checks. Package component: `crypto_bn256`. Public namespace:
`forge::crypto::bn256`.

```cpp
import forge.crypto.bn256;

const auto result = forge::crypto::bn256::pairing_check(pairs);
```

The target depends only on the private BN256 backend. It does not pull OpenSSL,
GMP or the rest of the Crypto family. `test_forge_crypto_bn256` covers golden
operations and malformed inputs; the package test verifies isolated installed
consumption.
