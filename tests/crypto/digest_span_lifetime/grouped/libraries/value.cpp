void dangling_assignment(std::span<const std::uint8_t> data) {
   std::span<const std::uint8_t> assigned;
   assigned = (sha256::hash(data).to_uint8_span());
   assigned = (((sha256::hash(data).to_uint8_span())));
}

void dangling_direct(std::span<const std::uint8_t> data) {
   const auto simple((sha256::hash(data).to_uint8_span()));
   const auto nested((((sha256::hash(data).to_uint8_span()))));
}

void dangling_list(std::span<const std::uint8_t> data) {
   const auto simple{(sha256::hash(data).to_uint8_span())};
   const auto nested{(((sha256::hash(data).to_uint8_span())))};
}

std::span<const std::uint8_t> dangling_return(std::span<const std::uint8_t> data) {
   return (sha256::hash(data).to_uint8_span());
}

std::span<const std::uint8_t> dangling_nested_return(std::span<const std::uint8_t> data) {
   return (((sha256::hash(data).to_uint8_span())));
}

task<std::span<const std::uint8_t>> dangling_co_return(std::span<const std::uint8_t> data) {
   co_return (sha256::hash(data).to_uint8_span());
}

task<std::span<const std::uint8_t>> dangling_nested_co_return(std::span<const std::uint8_t> data) {
   co_return (((sha256::hash(data).to_uint8_span())));
}
