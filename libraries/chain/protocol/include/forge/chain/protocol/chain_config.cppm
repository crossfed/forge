module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.chain_config;

import forge.chain.protocol.blockchain_parameters;
import forge.raw.codec;

namespace forge::chain::protocol::detail {

[[nodiscard]] constexpr blockchain_parameters default_chain_config_parameters() noexcept {
   return {
       .max_block_net_usage = 1'024U * 1'024U,
       .target_block_net_usage_pct = 1'000U,
       .max_transaction_net_usage = 512U * 1'024U,
       .base_per_transaction_net_usage = 12U,
       .net_usage_leeway = 500U,
       .context_free_discount_net_usage_num = 20U,
       .context_free_discount_net_usage_den = 100U,
       .max_block_cpu_usage = 200'000U,
       .target_block_cpu_usage_pct = 1'000U,
       .max_transaction_cpu_usage = 150'000U,
       .min_transaction_cpu_usage = 100U,
       .max_transaction_lifetime = 3'600U,
       .deferred_trx_expiration_window = 600U,
       .max_transaction_delay = 3'888'000U,
       .max_inline_action_size = 512U * 1'024U,
       .max_inline_action_depth = 4U,
       .max_authority_depth = 6U,
   };
}

} // namespace forge::chain::protocol::detail

export namespace forge::chain::protocol {

struct chain_config : blockchain_parameters {
   constexpr chain_config() noexcept : blockchain_parameters{detail::default_chain_config_parameters()} {}

   std::uint32_t max_action_return_value_size = 256U;

   bool operator==(const chain_config&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const chain_config& value) {
   forge::raw::pack(stream, static_cast<const blockchain_parameters&>(value));
   forge::raw::pack(stream, value.max_action_return_value_size);
}

template <typename Stream> void raw_unpack(Stream& stream, chain_config& value) {
   forge::raw::unpack(stream, static_cast<blockchain_parameters&>(value));
   forge::raw::unpack(stream, value.max_action_return_value_size);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(chain_config, (blockchain_parameters), (max_action_return_value_size))
} // namespace forge::chain::protocol
#endif
