# forge_codec_base64

`forge_codec_base64` provides strict RFC 4648 Base64 and Base64URL encoding for
host applications and wasm32 contracts from one implementation.

## Public API

- Target: `forge_codec_base64`
- Component: `codec_base64`
- Module: `forge.codec.base64`
- Namespace: `forge::codec::base64`

```cpp
import forge.codec.base64;

auto encoded = forge::codec::base64::encode("hello");
auto url = forge::codec::base64::encode(
   "hello",
   {.characters = forge::codec::base64::alphabet::url,
    .pad = forge::codec::base64::padding::omit});
auto bytes = forge::codec::base64::decode(encoded);
```

Use explicit options for URL alphabets, omitted padding, line wrapping and
whitespace-tolerant MIME input. Invalid or non-canonical input is rejected.

The library owns binary-to-text conversion only. Hashing, signatures, PEM objects and contract
runtime policy remain in their respective libraries.
