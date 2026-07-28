void holder::dangling(std::span<const std::uint8_t> data) {
   bytes = sha256::hash(data).to_uint8_span();
}
