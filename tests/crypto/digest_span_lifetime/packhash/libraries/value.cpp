void assign_packhash(const value_type& value) {
   assigned = sha256::packhash(value).to_uint8_span();
}

std::span<const std::uint8_t> return_packhash(bool flag, const value_type& value, const sha256& live) {
   return flag ? live.to_uint8_span() : sha256::packhash(value).to_uint8_span();
}
