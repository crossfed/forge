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

std::span<const std::uint8_t> wrap_lvalue(const sha256& value) {
   return std::span<const std::uint8_t>(value.to_uint8_span());
}

task<std::span<const std::uint8_t>> co_wrap_lvalue(const sha256& value) {
   co_return std::span<const std::uint8_t>{value.to_uint8_span()};
}

void consume_wrapped_temporary(std::span<const std::uint8_t> data) {
   consume(std::span{sha256::hash(data).to_uint8_span()});
}

void initialize_wrapped_lvalue(const sha256& value) {
   const auto direct(std::span<const std::uint8_t>(value.to_uint8_span()));
   const auto list{std::span<const std::uint8_t>{value.to_uint8_span()}};
}

std::span<const std::uint8_t> return_grouped_lvalue(const sha256& value) {
   return ((value.to_uint8_span()));
}

bytes return_grouped_copy(std::span<const std::uint8_t> data) {
   return ((copy(sha256::hash(data).to_uint8_span())));
}

task<std::span<const std::uint8_t>> co_return_grouped_lvalue(const sha256& value) {
   co_return ((std::span{value.to_uint8_span()}));
}

void use_grouped_safely(std::span<const std::uint8_t> data, const sha256& value) {
   consume(((sha256::hash(data).to_uint8_span())));
   const auto direct((copy(sha256::hash(data).to_uint8_span())));
   const auto list{((copy(sha256::hash(data).to_uint8_span())))};
   std::span<const std::uint8_t> assigned;
   assigned = ((value.to_uint8_span()));
}

bytes use_conditional_safely(bool flag, std::span<const std::uint8_t> data, const sha256& value) {
   consume(flag ? sha256::hash(data).to_uint8_span() : value.to_uint8_span());
   const auto selected = flag ? value.to_uint8_span() : value.to_uint8_span();
   return copy(flag ? sha256::hash(data).to_uint8_span() : value.to_uint8_span());
}

bytes use_copy_list_safely(std::span<const std::uint8_t> data, const sha256& value) {
   const auto copied = bytes{sha256::hash(data).to_uint8_span()};
   std::span<const std::uint8_t> selected = {value.to_uint8_span()};
   consume({sha256::packhash(data).to_uint8_span()});
   return copied;
}
