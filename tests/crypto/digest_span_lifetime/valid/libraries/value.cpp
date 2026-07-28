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

task<std::span<const std::uint8_t>> co_return_lvalue(const sha256& value) {
   co_return value.to_uint8_span();
}

task<bytes> co_return_copy(std::span<const std::uint8_t> data) {
   co_return copy(sha256::hash(data).to_uint8_span());
}

void initialize_owned_copy(std::span<const std::uint8_t> data) {
   auto direct_copy(copy(sha256::hash(data).to_uint8_span()));
   auto list_copy{copy(sha256::hash(data).to_uint8_span())};
}
