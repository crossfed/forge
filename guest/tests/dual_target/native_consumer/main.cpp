#include <cassert>
#include <cstdint>
#include <product/chain/constants.hpp>

import forge.raw.codec;
import product.chain.protocol;

int main() {
   const auto payload = product::chain::begin_revision{
       .workspace = product::chain::workspace_id{7},
       .inode = product::chain::inode_id{11},
       .size = 4096,
   };
   const auto encoded = forge::raw::pack(payload);
   assert(encoded.size() == 24);
   static_assert(PRODUCT_CHAIN_DEFAULT_BLOCK_SIZE == 4096);
}
