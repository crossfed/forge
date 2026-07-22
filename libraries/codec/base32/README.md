# Forge Codec Base32

`forge_codec_base32` provides RFC 4648-style Base32 encoding and decoding as a
general byte/text codec. Package component: `codec_base32`. Public module and
namespace: `forge.codec.base32` and `forge::codec::base32`.

```cpp
#include <array>
#include <cstdint>

import forge.codec.base32;

const auto input = std::array<std::uint8_t, 3>{'f', 'o', 'o'};
const auto encoded = forge::codec::base32::encode(input);
```

Encoding supports lower/upper alphabets and optional padding. Malformed options
raise typed Codec exceptions. The target depends only on `forge_exceptions` and
does not depend on Crypto or OpenSSL. `test_forge_codec_base32` covers vectors,
case, padding and invalid input; the package test proves isolated consumption.
