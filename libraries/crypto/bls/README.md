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

`forge.crypto.bls` also owns proof-of-possession validation and grouped
aggregate verification. A raw public key becomes a
`proof_verified_public_key` only through
`verify_proof_of_possession(key, proof)`. The wrapper is an operation-only
capability: it is not default-constructible, publicly constructible or
serializable.

Each aggregate group combines proof-verified public keys over one message,
while one aggregate signature covers all groups. Group messages must be
distinct; signers of the same message belong in one group. Requiring
proof-verified keys prevents rogue-key attacks in same-message aggregation.
Consensus libraries pass these typed keys and message bytes to this API and
never access the private BLS12-381 backend directly.

The target depends on Crypto Core and Digest, Codec Base64, Raw, Reflect,
Variant, Exceptions, OpenSSL Crypto and the private BLS12-381 backend. It does
not own consensus or finality policy. `test_forge_crypto_bls` preserves legacy
vectors, value encodings and contract operation behavior.
