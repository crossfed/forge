module;

#ifndef NDEBUG
#error "host Release must compile guest contract libraries with Release flags"
#endif

module product.contract.configuration;

namespace product::contract::configuration {

void verify(forge::contract::context&, const request&) {}

} // namespace product::contract::configuration
