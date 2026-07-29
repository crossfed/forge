#include <cstdint>
#include <span>
#include <string>

import forge.crypto.digest.sha256;

int main() {
   std::span<const std::uint8_t, forge::crypto::digest::sha256::byte_size> bytes(
       forge::crypto::digest::sha256::hash(std::string{"forge"}).to_uint8_span());
   return bytes.front();
}
