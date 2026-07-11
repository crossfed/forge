#include <cstdint>
#include <vector>

import forge.compression.zlib;

int main() {
   const auto input = std::vector<std::uint8_t>{'o', 'k'};
   const auto compressed = forge::compression::zlib_compress(input);
   const auto decompressed = forge::compression::zlib_decompress(compressed);
   return decompressed == input ? 0 : 1;
}
