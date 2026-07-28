void dangling_initializers(std::span<const std::uint8_t> data) {
   std::span<const std::uint8_t> assigned;
   assigned = std::span<const std::uint8_t>(sha256::hash(data).to_uint8_span());
   const auto direct(std::span<const std::uint8_t>(sha256::hash(data).to_uint8_span()));
   const auto list{std::span{sha256::hash(data).to_uint8_span()}};
}

std::span<const std::uint8_t> dangling_return(std::span<const std::uint8_t> data) {
   return std::span<const std::uint8_t>(sha256::hash(data).to_uint8_span());
}

task<std::span<const std::uint8_t>> dangling_co_return(std::span<const std::uint8_t> data) {
   co_return std::span{sha256::hash(data).to_uint8_span()};
}
