void copy_list_initialize(std::span<const std::uint8_t> data) {
   std::span<const std::uint8_t> declared = {sha256::hash(data).to_uint8_span()};
   assigned = {sha512::hash(data).to_uint8_span()};
   member.bytes = {{digest(data).to_span()}};
}

std::span<const std::uint8_t> copy_list_return(std::span<const std::uint8_t> data) {
   return {sha256::hash(data).to_uint8_span()};
}
