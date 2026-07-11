#include <concepts>
#include <cstdint>
#include <vector>

import forge.chain.protocol.block;
import forge.chain.protocol.fixed_key;
import forge.chain.protocol.transaction;

int main() {
   static_assert(std::same_as<forge::chain::protocol::bytes, std::vector<std::uint8_t>>);
   const auto digest = forge::chain::protocol::digest::hash("package-chain-protocol");
   auto transaction = forge::chain::protocol::transaction{};
   auto block = forge::chain::protocol::signed_block{};
   auto key = forge::chain::protocol::key256{};
   block.transaction_mroot = digest;
   (void)transaction;
   (void)block;
   (void)key;
   return 0;
}
