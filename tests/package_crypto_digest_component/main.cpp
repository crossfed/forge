#include <cstdint>
#include <span>
#include <string>

import forge.crypto.digest.sha256;

namespace {

void consume(std::span<const std::uint8_t>) {}

} // namespace

int main() {
   const auto value = forge::crypto::digest::sha256::hash(std::string{"forge"});
   consume(forge::crypto::digest::sha256::hash(std::string{"immediate"}).to_uint8_span());

   auto encoder = forge::crypto::digest::sha256::encoder{};
   const auto input = std::string{"named"};
   encoder.write(input.data(), static_cast<std::uint32_t>(input.size()));
   const auto encoded = encoder.result();
   const auto encoded_bytes = encoded.to_uint8_span();

   if (encoded_bytes.size() != forge::crypto::digest::sha256::byte_size) {
      return 1;
   }
   return value.data_size() == 32 ? 0 : 1;
}
