module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <string_view>

export module forge.chain.api.limits;

export import forge.chain.api.exceptions;
export import forge.chain.protocol.audit;

import forge.raw.raw;

export namespace forge::chain::api {

template <typename Value>
void require_request_within_limits(const Value& value, const protocol::service_limits& limits) {
   const auto bytes = forge::raw::pack_size(value);
   if (bytes > limits.max_request_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API request exceeds the configured byte limit",
                            forge::exceptions::ctx("bytes", bytes),
                            forge::exceptions::ctx("limit", limits.max_request_bytes));
   }
}

template <typename Value>
void require_response_within_limits(const Value& value, const protocol::service_limits& limits) {
   const auto bytes = forge::raw::pack_size(value);
   if (bytes > limits.max_response_bytes) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API response exceeds the configured byte limit",
                            forge::exceptions::ctx("bytes", bytes),
                            forge::exceptions::ctx("limit", limits.max_response_bytes));
   }
}

} // namespace forge::chain::api
