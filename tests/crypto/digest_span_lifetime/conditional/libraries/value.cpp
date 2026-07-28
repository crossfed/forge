void assign_conditional(bool flag, std::span<const std::uint8_t> data, const sha256& live) {
   assigned = flag ? sha256::hash(data).to_uint8_span() : live.to_uint8_span();
   assigned = flag ? live.to_uint8_span() : sha512::hash(data).to_uint8_span();
   assigned = flag ? (flag ? digest(data).to_span() : live.to_uint8_span()) : live.to_uint8_span();
}

void initialize_conditional(bool flag, std::span<const std::uint8_t> data, const sha256& live) {
   const auto direct(flag ? sha256::hash(data).to_uint8_span() : live.to_uint8_span());
   const auto list{flag ? live.to_uint8_span() : sha512::hash(data).to_uint8_span()};
}

std::span<const std::uint8_t> return_conditional(bool flag, std::span<const std::uint8_t> data,
                                                 const sha256& live) {
   return flag ? sha256::hash(data).to_uint8_span() : live.to_uint8_span();
}

task<std::span<const std::uint8_t>> co_return_conditional(bool flag, std::span<const std::uint8_t> data,
                                                          const sha256& live) {
   co_return flag ? live.to_uint8_span() : sha512::hash(data).to_uint8_span();
}
