# forge_codec_hex

`forge_codec_hex` provides strict hexadecimal encoding for host applications
and wasm32 contracts.

## Public API

- Target: `forge_codec_hex`
- Component: `codec_hex`
- Module: `forge.codec.hex`
- Namespace: `forge::codec::hex`

```cpp
import forge.codec.hex;

auto text = forge::codec::hex::encode(bytes);
auto upper = forge::codec::hex::encode(bytes, forge::codec::hex::letter_case::upper);
auto decoded = forge::codec::hex::decode(text);
auto version = forge::codec::hex::encode(std::uint32_t{1}, 8);
```

Decode consumes the complete input, requires an even number of digits and
rejects insufficient output buffers.
