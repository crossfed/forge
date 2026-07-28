void consume_wrapped_temporary_from_hxx(std::span<const std::uint8_t> data) {
   consume(std::span<const std::uint8_t>(sha256::hash(data).to_uint8_span()));
}
