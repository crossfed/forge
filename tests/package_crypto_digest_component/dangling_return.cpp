#include <cstdint>
#include <span>
#include <string>

import forge.crypto.digest.sha256;

std::span<const std::uint8_t, forge::crypto::digest::sha256::byte_size> dangling() {
   return forge::crypto::digest::sha256::hash(std::string{"forge"}).to_uint8_span();
}

int main() {
   return dangling().front();
}
