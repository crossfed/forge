module;

#include <forge/contract/intrinsics.h>

#include <cstdint>
#include <optional>
#include <vector>

export module forge.contract.privileged;

export import forge.contract.fixed_bytes;
export import forge.contract.producer_schedule;

import forge.contract.datastream;
import forge.contract.intrinsics;

export namespace forge::contract {

struct blockchain_parameters {
   std::uint64_t max_block_net_usage = 0;
   std::uint32_t target_block_net_usage_pct = 0;
   std::uint32_t max_transaction_net_usage = 0;
   std::uint32_t base_per_transaction_net_usage = 0;
   std::uint32_t net_usage_leeway = 0;
   std::uint32_t context_free_discount_net_usage_num = 0;
   std::uint32_t context_free_discount_net_usage_den = 0;
   std::uint32_t max_block_cpu_usage = 0;
   std::uint32_t target_block_cpu_usage_pct = 0;
   std::uint32_t max_transaction_cpu_usage = 0;
   std::uint32_t min_transaction_cpu_usage = 0;
   std::uint32_t max_transaction_lifetime = 0;
   std::uint32_t deferred_trx_expiration_window = 0;
   std::uint32_t max_transaction_delay = 0;
   std::uint32_t max_inline_action_size = 0;
   std::uint16_t max_inline_action_depth = 0;
   std::uint16_t max_authority_depth = 0;
};

template <typename Stream> void raw_pack(Stream& stream, const blockchain_parameters& value) {
   ::forge::raw::pack(stream, value.max_block_net_usage);
   ::forge::raw::pack(stream, value.target_block_net_usage_pct);
   ::forge::raw::pack(stream, value.max_transaction_net_usage);
   ::forge::raw::pack(stream, value.base_per_transaction_net_usage);
   ::forge::raw::pack(stream, value.net_usage_leeway);
   ::forge::raw::pack(stream, value.context_free_discount_net_usage_num);
   ::forge::raw::pack(stream, value.context_free_discount_net_usage_den);
   ::forge::raw::pack(stream, value.max_block_cpu_usage);
   ::forge::raw::pack(stream, value.target_block_cpu_usage_pct);
   ::forge::raw::pack(stream, value.max_transaction_cpu_usage);
   ::forge::raw::pack(stream, value.min_transaction_cpu_usage);
   ::forge::raw::pack(stream, value.max_transaction_lifetime);
   ::forge::raw::pack(stream, value.deferred_trx_expiration_window);
   ::forge::raw::pack(stream, value.max_transaction_delay);
   ::forge::raw::pack(stream, value.max_inline_action_size);
   ::forge::raw::pack(stream, value.max_inline_action_depth);
   ::forge::raw::pack(stream, value.max_authority_depth);
}

template <typename Stream> void raw_unpack(Stream& stream, blockchain_parameters& value) {
   ::forge::raw::unpack(stream, value.max_block_net_usage);
   ::forge::raw::unpack(stream, value.target_block_net_usage_pct);
   ::forge::raw::unpack(stream, value.max_transaction_net_usage);
   ::forge::raw::unpack(stream, value.base_per_transaction_net_usage);
   ::forge::raw::unpack(stream, value.net_usage_leeway);
   ::forge::raw::unpack(stream, value.context_free_discount_net_usage_num);
   ::forge::raw::unpack(stream, value.context_free_discount_net_usage_den);
   ::forge::raw::unpack(stream, value.max_block_cpu_usage);
   ::forge::raw::unpack(stream, value.target_block_cpu_usage_pct);
   ::forge::raw::unpack(stream, value.max_transaction_cpu_usage);
   ::forge::raw::unpack(stream, value.min_transaction_cpu_usage);
   ::forge::raw::unpack(stream, value.max_transaction_lifetime);
   ::forge::raw::unpack(stream, value.deferred_trx_expiration_window);
   ::forge::raw::unpack(stream, value.max_transaction_delay);
   ::forge::raw::unpack(stream, value.max_inline_action_size);
   ::forge::raw::unpack(stream, value.max_inline_action_depth);
   ::forge::raw::unpack(stream, value.max_authority_depth);
}

inline void set_blockchain_parameters(const blockchain_parameters& parameters) {
   const auto bytes = ::forge::raw::pack(parameters);
   ::set_blockchain_parameters_packed(reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data())), bytes.size());
}

inline void get_blockchain_parameters(blockchain_parameters& parameters) {
   auto bytes = std::vector<std::uint8_t>(::get_blockchain_parameters_packed(nullptr, 0U));
   if (!bytes.empty()) {
      const auto size = ::get_blockchain_parameters_packed(reinterpret_cast<char*>(bytes.data()), bytes.size());
      check(size <= bytes.size(), "blockchain parameter buffer is too small");
      bytes.resize(size);
   }
   parameters = ::forge::raw::unpack_exact<blockchain_parameters>(bytes);
}

inline void get_resource_limits(chain::protocol::name account, std::int64_t& ram_bytes, std::int64_t& net_weight,
                                std::int64_t& cpu_weight) {
   ::get_resource_limits(account.value, &ram_bytes, &net_weight, &cpu_weight);
}

inline void set_resource_limits(chain::protocol::name account, std::int64_t ram_bytes, std::int64_t net_weight,
                                std::int64_t cpu_weight) {
   ::set_resource_limits(account.value, ram_bytes, net_weight, cpu_weight);
}

[[nodiscard]] inline std::optional<std::uint64_t> set_proposed_producers(const std::vector<producer_key>& producers) {
   const auto bytes = ::forge::raw::pack(producers);
   const auto version = ::set_proposed_producers(reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data())),
                                                 bytes.size());
   return version < 0 ? std::nullopt : std::optional<std::uint64_t>{static_cast<std::uint64_t>(version)};
}

[[nodiscard]] inline std::optional<std::uint64_t>
set_proposed_producers(const std::vector<producer_authority>& producers) {
   const auto bytes = ::forge::raw::pack(producers);
   const auto version = ::set_proposed_producers_ex(
       1U, reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data())), bytes.size());
   return version < 0 ? std::nullopt : std::optional<std::uint64_t>{static_cast<std::uint64_t>(version)};
}

[[nodiscard]] inline bool is_privileged(chain::protocol::name account) {
   return ::is_privileged(account.value);
}

inline void set_privileged(chain::protocol::name account, bool privileged) {
   ::set_privileged(account.value, privileged);
}

inline void preactivate_feature(const checksum256& digest) {
   ::preactivate_feature(reinterpret_cast<const capi_checksum256*>(digest.data()));
}

} // namespace forge::contract
