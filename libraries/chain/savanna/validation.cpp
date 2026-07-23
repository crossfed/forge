module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <limits>
#include <vector>

module forge.chain.savanna.validation;

import forge.crypto.digest.sha256;
import forge.raw.raw;

namespace forge::chain::savanna {
namespace {

digest leaf_digest(const validation_leaf& leaf) {
   return forge::crypto::digest::sha256::hash(leaf);
}

} // namespace

validation_state make_validation(const validation_leaf& genesis) {
   auto result = validation_state{.first = genesis.num};
   result.tree.append(leaf_digest(genesis));
   result.roots.push_back(result.tree.root());
   result.leaves.push_back(genesis);
   return result;
}

validation_state append(validation_state state, const validation_leaf& leaf) {
   validate(state);
   if (state.roots.size() > std::numeric_limits<block_num_t>::max() - state.first ||
       leaf.num != state.first + static_cast<block_num_t>(state.roots.size())) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state, "Savanna validation leaf is not contiguous");
   }
   state.tree.append(leaf_digest(leaf));
   state.roots.push_back(state.tree.root());
   state.leaves.push_back(leaf);
   return state;
}

digest root_at(const validation_state& state, block_num_t num) {
   if (num < state.first || static_cast<std::size_t>(num - state.first) >= state.roots.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state,
                            "Savanna validation root lies outside retained range");
   }
   return state.roots[static_cast<std::size_t>(num - state.first)];
}

void validate(const validation_state& state) {
   if (state.tree.empty() || state.roots.empty() || state.roots.size() != state.leaves.size() ||
       state.tree.size() != state.roots.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state, "Savanna validation Merkle state is inconsistent");
   }
   if (state.roots.size() - 1U > std::numeric_limits<block_num_t>::max() - state.first) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state, "Savanna validation block range overflows");
   }

   auto replay = forge::chain::core::incremental_merkle_tree{};
   for (auto index = std::size_t{}; index < state.leaves.size(); ++index) {
      if (state.leaves[index].num != state.first + static_cast<block_num_t>(index)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state,
                               "Savanna retained validation leaf is not contiguous",
                               forge::exceptions::ctx("leaf_index", index));
      }
      replay.append(leaf_digest(state.leaves[index]));
      if (replay.root() != state.roots[index]) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state, "Savanna retained validation root is inconsistent",
                               forge::exceptions::ctx("root_index", index));
      }
   }
   if (forge::raw::pack(replay) != forge::raw::pack(state.tree)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state,
                            "Savanna incremental validation tree is inconsistent");
   }
}

} // namespace forge::chain::savanna
