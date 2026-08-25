#include <concepts>
#include <cstdint>
#include <flat_map>
#include <optional>
#include <string>
#include <vector>

import forge.chain.protocol.action;
import forge.chain.protocol.action_receipt;
import forge.chain.protocol.block;
import forge.chain.protocol.chain_config;
import forge.chain.protocol.entity_selector;
import forge.chain.protocol.fixed_key;
import forge.chain.protocol.float128;
import forge.chain.protocol.float64;
import forge.chain.protocol.native_ids;
import forge.chain.protocol.ratio;
import forge.chain.protocol.state_query;
import forge.chain.protocol.transaction;
import forge.chain.protocol.wasm_parameters;
import forge.variant.value;

bool producer_authority_json_roundtrip();

template <typename T>
concept has_account_member = requires(T value) { value.account; };

int main() {
   static_assert(std::same_as<forge::chain::protocol::bytes, std::vector<std::uint8_t>>);
   static_assert(std::same_as<decltype(forge::chain::protocol::table_scope_request{}.cursor),
                              std::optional<forge::chain::protocol::bytes>>);
   static_assert(std::same_as<decltype(forge::chain::protocol::table_scope_response{}.next),
                              std::optional<forge::chain::protocol::bytes>>);
   static_assert(std::same_as<decltype(forge::chain::protocol::table_changes_request{}.cursor),
                              std::optional<forge::chain::protocol::bytes>>);
   static_assert(std::same_as<decltype(forge::chain::protocol::account_changes_request{}.cursor),
                              std::optional<forge::chain::protocol::bytes>>);
   static_assert(std::same_as<decltype(forge::chain::protocol::table_mutation{}.table),
                              forge::chain::protocol::table_change_selector>);
   static_assert(std::same_as<decltype(forge::chain::protocol::table_changes_response{}.blocks),
                              std::vector<forge::chain::protocol::table_change_batch>>);
   static_assert(std::same_as<decltype(forge::chain::protocol::account_changes_response{}.blocks),
                              std::vector<forge::chain::protocol::account_change_batch>>);
   static_assert(
       std::same_as<decltype(forge::chain::protocol::account_response{}.state), forge::chain::protocol::account_state>);
   static_assert(!has_account_member<forge::chain::protocol::account_state>);
   static_assert(static_cast<std::uint8_t>(forge::chain::protocol::audit_class::state_point) == 2U);
   static_assert(static_cast<std::uint8_t>(forge::chain::protocol::audit_class::state_range) == 3U);
   static_assert(static_cast<std::uint8_t>(forge::chain::protocol::audit_class::state_changes) == 4U);
   static_assert(std::same_as<forge::chain::protocol::account_id, forge::db::ids::typed_id<1, 10>>);
   static_assert(std::same_as<forge::chain::protocol::resource_state_id, forge::db::ids::typed_id<1, 63>>);
   const auto digest = forge::chain::protocol::digest::hash(std::string{"package-chain-protocol"});
   const auto selector = forge::chain::protocol::account_selector{
       .id = forge::chain::protocol::account_id{42},
   };
   const auto config = forge::chain::protocol::chain_config{};
   const auto wasm = forge::chain::protocol::wasm_parameters{};
   const auto ratio = forge::chain::protocol::ratio{};
   const auto float64 = forge::chain::protocol::float64{.bits = 0x3ff0000000000000ULL};
   const auto float128 = forge::chain::protocol::float128{.bits = forge::chain::protocol::uint128_t{1U} << 112U};
   const auto float64_key = forge::chain::protocol::ordered_key(float64);
   const auto float128_key = forge::chain::protocol::ordered_key(float128);
   auto float64_variant = forge::variant{};
   auto float128_variant = forge::variant{};
   forge::to_variant(float64, float64_variant);
   forge::to_variant(float128, float128_variant);
   auto decoded_float64 = forge::chain::protocol::float64{};
   auto decoded_float128 = forge::chain::protocol::float128{};
   forge::from_variant(float64_variant, decoded_float64);
   forge::from_variant(float128_variant, decoded_float128);
   try {
      static_cast<void>(forge::chain::protocol::ordered_key(
          forge::chain::protocol::float64{.bits = 0x7ff8000000000000ULL}));
      return 1;
   } catch (const forge::chain::protocol::exceptions::unordered_value&) {
   }
   auto transaction = forge::chain::protocol::transaction{};
   auto action = forge::chain::protocol::action{};
   auto receipt = forge::chain::protocol::action_receipt{};
   receipt.auth_sequence.emplace(forge::chain::protocol::account_name{1U}, 1U);
   receipt.act_digest = forge::chain::protocol::generate_action_digest(action, forge::chain::protocol::bytes{});
   const auto savanna_digest = forge::chain::protocol::calculate_savanna_action_digest(receipt, action);
   auto block = forge::chain::protocol::signed_block{};
   auto key = forge::chain::protocol::key256::make_from_word_sequence<forge::chain::protocol::uint128_t>(
       forge::chain::protocol::uint128_t{1U}, forge::chain::protocol::uint128_t{2U});
   block.transaction_mroot = digest;
   (void)transaction;
   (void)savanna_digest;
   (void)block;
   (void)key;
   (void)config;
   (void)wasm;
   (void)ratio;
   (void)float64_key;
   (void)float128_key;
   return forge::chain::protocol::selects_exactly_one(selector) && decoded_float64 == float64 &&
                  decoded_float128 == float128 && producer_authority_json_roundtrip()
              ? 0
              : 1;
}
