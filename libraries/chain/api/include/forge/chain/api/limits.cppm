module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

export module forge.chain.api.limits;

export import forge.chain.api.exceptions;
export import forge.chain.protocol.audit;
export import forge.chain.protocol.block_query;
export import forge.chain.protocol.state_query;
export import forge.chain.protocol.transaction_query;

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
   if constexpr (requires { value.audit; }) {
      if (value.audit) {
         const auto proof_bytes = forge::raw::pack_size(*value.audit);
         if (proof_bytes > limits.max_proof_bytes) {
            FORGE_THROW_EXCEPTION(
                exceptions::resource_exhausted, "chain API response proof exceeds the configured byte limit",
                forge::exceptions::ctx("bytes", proof_bytes), forge::exceptions::ctx("limit", limits.max_proof_bytes));
         }
      }
   }
}

void require_request_within_limits(const protocol::block_range_request& value, const protocol::service_limits& limits);
void require_request_within_limits(const protocol::protocol_features_request& value,
                                   const protocol::service_limits& limits);
void require_request_within_limits(const protocol::producers_request& value, const protocol::service_limits& limits);
void require_request_within_limits(const protocol::state_range_request& value, const protocol::service_limits& limits);
void require_request_within_limits(const protocol::state_changes_request& value,
                                   const protocol::service_limits& limits);
void require_request_within_limits(const protocol::table_rows_request& value, const protocol::service_limits& limits);
void require_request_within_limits(const protocol::table_scope_request& value, const protocol::service_limits& limits);
void require_request_within_limits(const protocol::scheduled_request& value, const protocol::service_limits& limits);
void require_request_within_limits(const protocol::authorizers_request& value, const protocol::service_limits& limits);
void require_request_within_limits(const protocol::transaction_await_request& value,
                                   const protocol::service_limits& limits);

void require_transaction_batch_within_limits(const std::vector<protocol::transaction_submit_request>& values,
                                             const protocol::service_limits& limits);

} // namespace forge::chain::api
