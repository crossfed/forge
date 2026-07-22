# Forge Crypto BLS

`forge_crypto_bls` owns BLS12-381 values, signatures and deterministic contract
primitives. Package component: `crypto_bls`. Public namespace:
`forge::crypto::bls`.

## Modules

- `forge.crypto.bls.values`
- `forge.crypto.bls`
- `forge.crypto.bls.primitives`

```cpp
import forge.crypto.bls.values;

const auto signature = forge::crypto::bls::signature_value{};
```

The target depends on Crypto Core and Digest, Codec Base64, Raw, Reflect,
Variant, Exceptions, OpenSSL Crypto and the private BLS12-381 backend. It does
not own consensus or finality policy. `test_forge_crypto_bls` preserves legacy
vectors, value encodings and contract operation behavior.
