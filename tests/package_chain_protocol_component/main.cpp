#include <concepts>
#include <cstdint>
#include <flat_map>
#include <vector>

import forge.chain.protocol.action;
import forge.chain.protocol.action_receipt;
import forge.chain.protocol.block;
import forge.chain.protocol.fixed_key;
import forge.chain.protocol.transaction;

int main() {
   static_assert(std::same_as<forge::chain::protocol::bytes, std::vector<std::uint8_t>>);
   const auto digest = forge::chain::protocol::digest::hash("package-chain-protocol");
   auto transaction = forge::chain::protocol::transaction{};
   auto action = forge::chain::protocol::action{};
   auto receipt = forge::chain::protocol::action_receipt{};
   receipt.auth_sequence.emplace(forge::chain::protocol::account_name{1U}, 1U);
   receipt.act_digest = forge::chain::protocol::generate_action_digest(action, forge::chain::protocol::bytes{});
   const auto savanna_digest = forge::chain::protocol::calculate_savanna_action_digest(receipt, action);
   auto block = forge::chain::protocol::signed_block{};
   auto key = forge::chain::protocol::key256{};
   block.transaction_mroot = digest;
   (void)transaction;
   (void)savanna_digest;
   (void)block;
   (void)key;
   return 0;
}
