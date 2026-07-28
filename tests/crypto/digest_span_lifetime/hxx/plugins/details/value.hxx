void dangling(std::span<const std::uint8_t> data) {
   const auto bytes = std::span<const std::uint8_t>{sha256::hash(data).to_uint8_span()};
}
