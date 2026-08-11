#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

import forge.chain.protocol.action;
import forge.chain.savanna.values;
import forge.raw.codec;
import product.chain.protocol;

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
   assert(std::ranges::equal(encoded, expected));

   const auto transaction_action = forge::chain::protocol::action{
       forge::chain::protocol::permission_level{},
       forge::chain::protocol::account_name{},
       payload,
   };
   assert(transaction_action.name == product::chain::begin_revision::get_name());
   assert(std::ranges::equal(transaction_action.data, encoded));

   const auto sum = product::chain::checked_add(7, 11);
   assert(sum && *sum == 18);
   assert(product::chain::supports_nonzero_sizes());
   assert(!product::chain::checked_add(std::numeric_limits<std::uint64_t>::max(), 1));

   const auto finalizer = forge::chain::savanna::finalizer{
       .description = "f",
       .weight = 7,
       .public_key = {},
   };
   const auto finalizer_wire = forge::raw::pack(finalizer);
   auto expected_finalizer_wire = std::array<std::uint8_t, 107>{};
   expected_finalizer_wire[0] = 1;
   expected_finalizer_wire[1] = static_cast<std::uint8_t>('f');
   expected_finalizer_wire[2] = 7;
   expected_finalizer_wire[10] = 96;
   assert(std::ranges::equal(finalizer_wire, expected_finalizer_wire));
}
