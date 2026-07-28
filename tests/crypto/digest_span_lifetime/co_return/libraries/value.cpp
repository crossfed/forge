task<std::span<const std::uint8_t>> dangling(std::span<const std::uint8_t> data) {
   co_return sha256::hash(data).to_uint8_span();
}
