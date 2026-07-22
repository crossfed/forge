# forge_codec_base58

`forge_codec_base58` provides strict Bitcoin-alphabet Base58 encoding for host
applications and wasm32 contracts.

## Public API

- Target: `forge_codec_base58`
- Component: `codec_base58`
- Module: `forge.codec.base58`
- Namespace: `forge::codec::base58`

```cpp
import forge.codec.base58;

auto encoded = forge::codec::base58::encode(bytes);
auto decoded = forge::codec::base58::decode(encoded);
```

The implementation is derived from Bitcoin Core's byte-vector algorithm. It
preserves leading zeroes and rejects whitespace and ambiguous non-alphabet
characters. It does not depend on OpenSSL big numbers or runtime scheduling.
