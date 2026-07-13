#include <span>

import forge.chain.core.merkle;

int main() {
   const auto leaf = forge::chain::core::digest::hash("package-chain-core-merkle");
   const auto root = forge::chain::core::calculate_merkle_root(std::span{&leaf, 1U});
   auto tree = forge::chain::core::incremental_merkle_tree{};
   tree.append(leaf);
   return root == tree.root() ? 0 : 1;
}
