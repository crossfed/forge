module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

module forge.chain.api.limits;

import forge.chain.api.table_key;

namespace forge::chain::api {
namespace {

void require_page(std::uint32_t value, std::uint32_t limit, std::string_view name) {
   if (value == 0U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "chain API page limit must be positive",
                            forge::exceptions::ctx("field", name));
   }
   if (value > limit) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "chain API page limit exceeds the configured maximum",
                            forge::exceptions::ctx("field", name), forge::exceptions::ctx("value", value),
                            forge::exceptions::ctx("limit", limit));
   }
}

template <typename Value> void require_packed_request(const Value& value, const protocol::service_limits& limits) {
   require_request_within_limits<Value>(value, limits);
}

bool bytes_less(const protocol::bytes& left, const protocol::bytes& right) {
   return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end());
}

} // namespace

void require_request_within_limits(const protocol::block_range_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
}

void require_request_within_limits(const protocol::protocol_features_request& value,
                                   const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
   if (value.lower_bound && value.upper_bound && *value.upper_bound < *value.lower_bound) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "protocol feature lower bound exceeds its upper bound");
   }
}

void require_request_within_limits(const protocol::producers_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
}

void require_request_within_limits(const protocol::state_range_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
   if (value.range.lower && value.range.upper && bytes_less(*value.range.upper, *value.range.lower)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "state range lower bound exceeds its upper bound");
   }
}

void require_request_within_limits(const protocol::state_changes_request& value,
                                   const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
   if (value.from_block > value.to_block) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "state changes interval is reversed");
   }
   if (value.ranges.size() > limits.max_state_batch_size) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "state changes range count exceeds the configured maximum",
                            forge::exceptions::ctx("count", value.ranges.size()),
                            forge::exceptions::ctx("limit", limits.max_state_batch_size));
   }
   for (const auto& range : value.ranges) {
      if (range.lower && range.upper && bytes_less(*range.upper, *range.lower)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_request, "state changes range lower bound exceeds its upper bound");
      }
   }
   const auto range_count = value.ranges.empty() ? std::size_t{1U} : value.ranges.size();
   if (value.cursor && (value.cursor->range >= range_count || value.cursor->block <= value.from_block ||
                        value.cursor->block > value.to_block)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "state changes cursor is outside the requested interval");
   }
}

void require_request_within_limits(const protocol::table_rows_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
   validate_table_rows_request(value);
}

void require_request_within_limits(const protocol::table_scope_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
}

void require_request_within_limits(const protocol::scheduled_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
}

void require_request_within_limits(const protocol::authorizers_request& value, const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   require_page(value.limit, limits.max_page_size, "limit");
   const auto inputs = value.accounts.size() + value.keys.size();
   if (inputs > limits.max_state_batch_size) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "authorizer input count exceeds the configured maximum",
                            forge::exceptions::ctx("count", inputs),
                            forge::exceptions::ctx("limit", limits.max_state_batch_size));
   }
}

void require_request_within_limits(const protocol::transaction_await_request& value,
                                   const protocol::service_limits& limits) {
   require_packed_request(value, limits);
   if (value.timeout_ms == 0U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "transaction await timeout must be positive");
   }
   if (value.timeout_ms > limits.max_await_ms) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "transaction await timeout exceeds the configured maximum",
                            forge::exceptions::ctx("timeout_ms", value.timeout_ms),
                            forge::exceptions::ctx("limit", limits.max_await_ms));
   }
}

void require_transaction_batch_within_limits(const std::vector<protocol::transaction_submit_request>& values,
                                             const protocol::service_limits& limits) {
   if (values.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_request, "transaction batch must not be empty");
   }
   if (values.size() > limits.max_transaction_batch_size) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "transaction batch count exceeds the configured maximum",
                            forge::exceptions::ctx("count", values.size()),
                            forge::exceptions::ctx("limit", limits.max_transaction_batch_size));
   }
   require_packed_request(values, limits);
   for (const auto& value : values) {
      require_packed_request(value, limits);
   }
}

} // namespace forge::chain::api
