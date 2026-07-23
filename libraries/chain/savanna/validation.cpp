module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <limits>

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
   return result;
}

validation_state append(validation_state state, const validation_leaf& leaf) {
   if (state.roots.size() > std::numeric_limits<block_num_t>::max() - state.first ||
       leaf.num != state.first + static_cast<block_num_t>(state.roots.size())) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state, "Savanna validation leaf is not contiguous");
   }
   state.tree.append(leaf_digest(leaf));
   state.roots.push_back(state.tree.root());
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
   if (state.tree.empty() || state.roots.empty() || state.tree.size() != state.roots.size() ||
       state.roots.back() != state.tree.root()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state, "Savanna validation Merkle state is inconsistent");
   }
   if (state.roots.size() - 1U > std::numeric_limits<block_num_t>::max() - state.first) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state, "Savanna validation block range overflows");
   }
}

} // namespace forge::chain::savanna
