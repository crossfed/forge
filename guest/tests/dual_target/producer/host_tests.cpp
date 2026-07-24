#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

import forge.raw.codec;
import forge.chain.protocol.action;
import product.chain.protocol;

namespace {

forge::chain::protocol::action_name runtime_action_name() {
   return product::chain::begin_revision::get_name();
}

} // namespace

int main() {
   const auto payload = product::chain::begin_revision{
       .workspace = product::chain::workspace_id{7},
       .inode = product::chain::inode_id{11},
       .size = 4096,
   };
   const auto encoded = forge::raw::pack(payload);
   const auto expected = std::array<std::uint8_t, 24>{
       7, 0, 0, 0, 0, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0,
   };
   assert(encoded.size() == expected.size());
   assert(std::equal(encoded.begin(), encoded.end(), expected.begin(), expected.end()));

   const auto transaction_action = forge::chain::protocol::action{
       forge::chain::protocol::permission_level{},
       forge::chain::protocol::account_name{},
       payload,
   };
   constexpr auto abi_action_name = product::chain::begin_revision::get_name();
   assert(runtime_action_name() != abi_action_name);
   assert(transaction_action.name == abi_action_name);
   assert(transaction_action.data.size() == encoded.size());
   assert(std::equal(transaction_action.data.begin(), transaction_action.data.end(), encoded.begin(), encoded.end()));

   const auto sum = product::chain::checked_add(7, 11);
   assert(sum && *sum == 18);
   assert(!product::chain::checked_add(std::numeric_limits<std::uint64_t>::max(), 1));
}
