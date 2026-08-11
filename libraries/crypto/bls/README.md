# Forge Crypto BLS

`forge_crypto_bls_values` owns the freestanding BLS12-381 values shared with
contracts. Package component: `crypto_bls_values`. The value module has no
BLS12-381 backend dependency.

`forge_crypto_bls` owns host-side key generation, signing, point validation,
verification, proof of possession, aggregation and text encoding. Package
component: `crypto_bls`. Public namespace: `forge::crypto::bls`. The host
serialization module exposes canonical public value text and Variant conversion
without exporting private-key or operational signing APIs.

## Modules

- `forge.crypto.bls.values`
- `forge.crypto.bls.exceptions`
- `forge.crypto.bls.serialization`
- `forge.crypto.bls`
- `forge.crypto.bls.primitives`

```cpp
import forge.crypto.bls.values;

const auto key = forge::crypto::bls::public_key{};
const auto bytes = key.bytes();
```

Consumers importing `forge.crypto.bls.values` link
`Forge::forge_crypto_bls_values`; consumers importing `forge.crypto.bls` link
`Forge::forge_crypto_bls`.

`public_key`, `signature` and `aggregate_signature` are fixed-size byte values.
They do not parse or validate points and do not expose backend objects. Their
raw encoding is a varuint length followed by exactly 96 or 192 bytes; decoding
rejects any other length.

Host text conversion is explicit through `forge::crypto::bls::encoding`. Import
`forge.crypto.bls.serialization` when only public value serialization is needed,
or `forge.crypto.bls` for the complete host crypto surface:

```cpp
import forge.crypto.bls;

const auto key = forge::crypto::bls::encoding::parse_public_key(text);
const auto canonical = forge::crypto::bls::encoding::format(key);
```

This is a pre-stable source and JSON break: BLS protocol values use canonical
`PUB_BLS_` and `SIG_BLS_` text. The former hexadecimal JSON representation and
the old host value wrappers have no compatibility reader or aliases. Raw
encoding remains unchanged.

The source migration is mechanical:

- construct text values with `encoding::parse_public_key`,
  `encoding::parse_signature` or `encoding::parse_aggregate_signature`;
- format values with `encoding::format` instead of `to_string()`;
- replace mutable `aggregate_signature::aggregate(...)` calls with
  `signature_accumulator::add(...)` followed by `finish()`;
- use `public_key::serialize()` or `bytes()` when a fixed byte projection is
  required.

Text parsing throws typed `forge::crypto::bls::exceptions::parse_error`.
Untrusted byte values should be passed to `valid`, `verify` or
`verify_proof_of_possession`; malformed points return `false` or `std::nullopt`.
The `verify(proof_verified_public_key, ...)` overload reuses the point retained
by the proof capability and does not decode the public key again.

Use `signature_accumulator` to build an `aggregate_signature`. It validates
every input and throws typed BLS exceptions for malformed signatures, empty
results or moved-from use. The accumulator and `proof_verified_public_key`
hide all BLS12-381 objects behind pimpl.

Grouped verification accepts only proof-verified public keys. Each group
combines signers over one message, and messages must be distinct between
groups. This prevents rogue-key attacks in same-message aggregation.

The host target depends on Crypto Core and Digest, Codec Base64, Raw, Variant,
Exceptions, OpenSSL Crypto and the private BLS12-381 backend. It owns no
consensus or finality policy.
