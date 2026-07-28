void ignored_build_output(std::span<const std::uint8_t> data) {
   const auto bytes = sha256::hash(data).to_uint8_span();
}
