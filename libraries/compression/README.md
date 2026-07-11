# forge_compression

`forge_compression` owns reusable byte-level compression helpers. It currently
provides zlib compression and decompression with typed limits and exceptions.

## When To Use

- A Forge library needs zlib-compressed byte payloads.
- Callers need bounded decompression to avoid unbounded output allocation.
- A higher-level protocol owns what is compressed, but wants shared zlib
  mechanics.

## When Not To Use

- Do not put chain-specific packed transaction rules here.
- Do not use this library as a file archive, content-addressed storage or
  streaming compression framework.
- Do not silently decompress untrusted data without an output limit.

## Public Modules

- `forge.compression.exceptions` - typed compression exceptions.
- `forge.compression.zlib` - zlib levels, limits, compress and decompress
  helpers.

Target: `forge_compression`.

Dependencies: `forge_exceptions`, Boost.Iostreams and `ZLIB::ZLIB`.

Package component: `compression`.

## Examples

### Compress And Decompress Bytes

```cpp
#include <span>
#include <string>

import forge.compression.zlib;

std::string text = "payload";
auto compressed = forge::compression::zlib_compress(
   std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});

auto restored = forge::compression::zlib_decompress(
   std::span<const std::uint8_t>{compressed.data(), compressed.size()},
   {.max_output_size = 1024 * 1024});
```

### Choose A Level

```cpp
std::string text = "payload";
std::span<const std::uint8_t> input{reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
auto compressed = forge::compression::zlib_compress(
   input,
   forge::compression::zlib_level::best_compression);
```

## Boundaries

- `forge_compression` only transforms byte spans.
- Protocol libraries decide which fields are compressed, which level is used and
  what decompression limit is appropriate.
- Backend zlib and Boost.Iostreams types do not appear in public APIs.

## Errors

- `forge::compression::exceptions::invalid_input` for malformed compressed data.
- `forge::compression::exceptions::output_limit` when decompression exceeds the
  configured limit.
- `forge::compression::exceptions::backend_error` for backend failures.

## Tests

`test_forge_compression` covers zlib roundtrip, empty input, invalid compressed
bytes and output-limit failures.

`test_forge_package_compression_component` verifies that installed consumers can
use `find_package(Forge CONFIG REQUIRED COMPONENTS compression)` and import
compression modules.
