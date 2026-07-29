#include <array>
#include <cstdint>
#include <span>

import forge.crypto.digest.sha256;

int main() {
   constexpr auto input = std::array<std::uint8_t, 1>{42};
   const auto bytes = forge::crypto::digest::sha256{std::span{input}}.to_uint8_span();
   return bytes.front();
}
