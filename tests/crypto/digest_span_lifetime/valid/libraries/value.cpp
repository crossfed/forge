void safe(std::span<const std::uint8_t> data) {
   consume(sha256::hash(data).to_uint8_span());
   const auto bytes = copy(sha256::hash(data).to_uint8_span());
}
