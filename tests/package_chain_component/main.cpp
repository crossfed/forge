#include <span>

import forge.chain.block;
import forge.chain.fixed_key;
import forge.chain.merkle;
import forge.chain.transaction;

int main() {
   const auto leaf = forge::chain::digest::hash("package-chain-merkle");
   const auto root = forge::chain::calculate_merkle_root(std::span{&leaf, 1U});
   forge::chain::incremental_merkle_tree tree;
   tree.append(leaf);

   forge::chain::transaction transaction;
   forge::chain::signed_block block;
   (void)root;
   (void)tree;
   forge::chain::key256 key;
   (void)transaction;
   (void)block;
   (void)key;
   return 0;
}
