#include <array>
#include <cstdint>

import forge.codec.base32;

int main() {
   const auto input = std::array<std::uint8_t, 3>{'f', 'o', 'o'};
   return forge::codec::base32::encode(input) == "mzxw6" ? 0 : 1;
}
