module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.blockchain_parameters;

export namespace forge::chain::protocol {

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

   bool operator==(const blockchain_parameters&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(blockchain_parameters, (),
                      (max_block_net_usage, target_block_net_usage_pct, max_transaction_net_usage,
                       base_per_transaction_net_usage, net_usage_leeway, context_free_discount_net_usage_num,
                       context_free_discount_net_usage_den, max_block_cpu_usage, target_block_cpu_usage_pct,
                       max_transaction_cpu_usage, min_transaction_cpu_usage, max_transaction_lifetime,
                       deferred_trx_expiration_window, max_transaction_delay, max_inline_action_size,
                       max_inline_action_depth, max_authority_depth))
} // namespace forge::chain::protocol
#endif
