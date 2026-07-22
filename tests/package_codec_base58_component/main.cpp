#include <array>
#include <cstdint>

import forge.codec.base58;

int main() {
   constexpr auto input = std::array<std::uint8_t, 4>{0U, 0U, 0U, 1U};
   return forge::codec::base58::encode(input) == "1112" ? 0 : 1;
}
