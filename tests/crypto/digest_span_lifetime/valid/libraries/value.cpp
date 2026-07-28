void safe(std::span<const std::uint8_t> data) {
   consume(sha256::hash(data).to_uint8_span());
   const auto bytes = copy(sha256::hash(data).to_uint8_span());
}

std::span<const std::uint8_t> return_lvalue(const sha256& value) {
   return value.to_uint8_span();
}

std::span<const std::uint8_t> holder::return_owned() const {
   return digest.to_uint8_span();
}

bytes return_copy(std::span<const std::uint8_t> data) {
   return copy(sha256::hash(data).to_uint8_span());
}
