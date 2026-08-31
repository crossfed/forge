module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>

module forge.chain.savanna.genesis;

import forge.crypto.digest.sha256;
import forge.raw.raw;

namespace forge::chain::savanna {
namespace {

inline constexpr auto percent_100 = std::uint32_t{10'000U};
inline constexpr auto percent_point_one = std::uint32_t{10U};
inline constexpr auto min_net_usage_delta = std::uint32_t{10U};
inline constexpr auto max_byte_array_size = std::uint32_t{20U * 1024U * 1024U};
inline constexpr auto max_proposers = std::size_t{64U * 1024U};
inline constexpr auto max_finalizers = std::size_t{64U * 1024U};

void require(bool condition, const char* message) {
   if (!condition) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_genesis, message);
   }
}

void validate_chain_config(const forge::chain::protocol::chain_config& value) {
   require(value.target_block_net_usage_pct <= percent_100,
           "Savanna genesis target block net usage percentage cannot exceed 100%");
   require(value.target_block_net_usage_pct >= percent_point_one,
           "Savanna genesis target block net usage percentage must be at least 0.1%");
   require(value.target_block_cpu_usage_pct <= percent_100,
           "Savanna genesis target block CPU usage percentage cannot exceed 100%");
   require(value.target_block_cpu_usage_pct >= percent_point_one,
           "Savanna genesis target block CPU usage percentage must be at least 0.1%");
   require(value.max_transaction_net_usage < value.max_block_net_usage,
           "Savanna genesis max transaction net usage must be less than max block net usage");
   require(value.max_transaction_cpu_usage < value.max_block_cpu_usage,
           "Savanna genesis max transaction CPU usage must be less than max block CPU usage");
   require(value.base_per_transaction_net_usage < value.max_transaction_net_usage,
           "Savanna genesis base transaction net usage must be less than the transaction maximum");
   require(value.max_transaction_net_usage - value.base_per_transaction_net_usage >= min_net_usage_delta,
           "Savanna genesis max transaction net usage must exceed base usage by at least ten bytes");
   require(value.context_free_discount_net_usage_den > 0U,
           "Savanna genesis context-free net usage denominator cannot be zero");
   require(value.context_free_discount_net_usage_num <= value.context_free_discount_net_usage_den,
           "Savanna genesis context-free net usage ratio cannot exceed one");
   require(value.min_transaction_cpu_usage <= value.max_transaction_cpu_usage,
           "Savanna genesis minimum transaction CPU usage cannot exceed the maximum");
   require(value.max_block_cpu_usage > value.min_transaction_cpu_usage &&
               value.max_transaction_cpu_usage < value.max_block_cpu_usage - value.min_transaction_cpu_usage,
           "Savanna genesis max transaction CPU usage is inconsistent with block CPU usage");
   require(value.max_authority_depth >= 1U, "Savanna genesis max authority depth must be positive");
   require(value.max_action_return_value_size <= max_byte_array_size,
           "Savanna genesis max action return value size exceeds the protocol limit");
}

void validate_wasm(const forge::chain::protocol::wasm_parameters& value) {
   require(value.max_section_elements >= 4U, "Savanna genesis wasm max_section_elements is too small");
   require(value.max_func_local_bytes >= 8U, "Savanna genesis wasm max_func_local_bytes is too small");
   require(value.max_nested_structures >= 1U, "Savanna genesis wasm max_nested_structures is too small");
   require(value.max_symbol_bytes >= 32U, "Savanna genesis wasm max_symbol_bytes is too small");
   require(value.max_module_bytes >= 256U, "Savanna genesis wasm max_module_bytes is too small");
   require(value.max_code_bytes >= 32U, "Savanna genesis wasm max_code_bytes is too small");
   require(value.max_pages >= 1U, "Savanna genesis wasm max_pages is too small");
   require(value.max_call_depth >= 2U, "Savanna genesis wasm max_call_depth is too small");
}

} // namespace

forge::chain::protocol::chain_id calculate_chain_id(const genesis& value) {
   return forge::crypto::digest::sha256::hash(forge::raw::pack(value));
}

void validate(const genesis& value) {
   validate_chain_config(value.configuration);
   validate_wasm(value.wasm);
   require(!value.proposers.producers.empty(), "Savanna genesis proposer schedule is empty");
   require(value.proposers.producers.size() <= max_proposers,
           "Savanna genesis proposer schedule exceeds the protocol limit");
   require(value.finalizers.generation == 1U, "Savanna genesis finalizer generation must be one");
   require(value.finalizers.finalizers.size() <= max_finalizers,
           "Savanna genesis finalizer policy exceeds the protocol limit");
   try {
      static_cast<void>(forge::chain::savanna::validate(finalizer_policy_state{
          .policy = value.finalizers,
          .proofs = value.finalizer_proofs,
      }));
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_genesis, "Savanna genesis finalizer policy is invalid",
                            forge::exceptions::ctx("reason", error.what()));
   }
}

} // namespace forge::chain::savanna
