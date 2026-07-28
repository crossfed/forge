std::span<const std::uint8_t> dangling_sha256(std::span<const std::uint8_t> data) {
   return sha256::hash(data).to_uint8_span();
}

std::span<const std::uint8_t> dangling_sha512(std::span<const std::uint8_t> data) {
   return sha512::hash(data).to_uint8_span();
}

std::span<const std::uint8_t> dangling_digest(std::span<const std::uint8_t> data) {
   return digest(data).to_span();
}
