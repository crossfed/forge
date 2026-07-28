void dangling(std::span<const std::uint8_t> data) {
   std::span<const std::uint8_t> bytes;
   bytes = sha256::hash(data).to_uint8_span();
}
