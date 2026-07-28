std::span<const std::uint8_t> wrap_lvalue_from_hxx(const sha256& value) {
   return std::span<const std::uint8_t>{value.to_uint8_span()};
}
