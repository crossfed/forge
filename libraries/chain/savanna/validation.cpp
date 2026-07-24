module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <limits>
#include <utility>
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

bool validation_state::empty() const noexcept {
   return roots_.empty();
}

block_num_t validation_state::first_block_num() const {
   if (empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state,
                            "empty Savanna validation state has no first block");
   }
   return first_;
}

block_num_t validation_state::current_block_num() const {
   if (empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state,
                            "empty Savanna validation state has no current block");
   }
   return first_ + static_cast<block_num_t>(roots_.size() - 1U);
}

std::size_t validation_state::retained_size() const noexcept {
   return roots_.size();
}

digest validation_state::root() const {
   return current_.root();
}

validation_state make_validation(const validation_leaf& genesis) {
   auto result = validation_state{};
   result.first_ = genesis.num;
   result.current_.append(leaf_digest(genesis));
   result.roots_.push_back(result.current_.root());
   result.leaves_.push_back(genesis);
   return result;
}

validation_state append(validation_state state, const validation_leaf& leaf) {
   if (state.empty()) {
      return make_validation(leaf);
   }
   if (state.roots_.size() > std::numeric_limits<block_num_t>::max() - state.first_ ||
       leaf.num != state.first_ + static_cast<block_num_t>(state.roots_.size())) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state, "Savanna validation leaf is not contiguous");
   }
   state.current_.append(leaf_digest(leaf));
   state.roots_.push_back(state.current_.root());
   state.leaves_.push_back(leaf);
   return state;
}

validation_state advance_finalized(validation_state state, block_num_t finalized) {
   if (state.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::validation_root_unavailable,
                            "empty Savanna validation state cannot advance finality");
   }
   if (finalized < state.first_ || finalized > state.current_block_num()) {
      FORGE_THROW_EXCEPTION(exceptions::validation_root_unavailable,
                            "Savanna finalized block lies outside retained validation range",
                            forge::exceptions::ctx("block", finalized));
   }
   if (finalized == state.first_) {
      return state;
   }

   const auto remove_count = static_cast<std::size_t>(finalized - state.first_);
   for (auto index = std::size_t{}; index < remove_count; ++index) {
      state.prefix_.append(leaf_digest(state.leaves_[index]));
   }
   state.roots_.erase(state.roots_.begin(),
                      state.roots_.begin() + static_cast<std::ptrdiff_t>(remove_count));
   state.leaves_.erase(state.leaves_.begin(),
                       state.leaves_.begin() + static_cast<std::ptrdiff_t>(remove_count));
   state.first_ = finalized;
   return state;
}

digest root_at(const validation_state& state, block_num_t num) {
   if (state.empty() || num < state.first_ ||
       static_cast<std::size_t>(num - state.first_) >= state.roots_.size()) {
      FORGE_THROW_EXCEPTION(exceptions::validation_root_unavailable,
                            "Savanna validation root lies outside retained range");
   }
   return state.roots_[static_cast<std::size_t>(num - state.first_)];
}

void validate(const validation_state& state) {
   if (state.empty()) {
      if (!state.prefix_.empty() || !state.current_.empty() || !state.leaves_.empty() ||
          state.first_ != 0U) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state,
                               "empty Savanna validation state is inconsistent");
      }
      return;
   }

   if (state.prefix_.size() > static_cast<std::uint64_t>(state.first_)) {
      FORGE_THROW_EXCEPTION(
          exceptions::invalid_validation_state,
          "Savanna validation prefix exceeds the available block history",
          forge::exceptions::ctx("prefix_size", state.prefix_.size()),
          forge::exceptions::ctx("first_block", state.first_));
   }

   if (state.current_.empty() || state.roots_.size() != state.leaves_.size() ||
       state.roots_.size() >
           std::numeric_limits<std::uint64_t>::max() - state.prefix_.size() ||
       state.current_.size() !=
           state.prefix_.size() + static_cast<std::uint64_t>(state.roots_.size())) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state, "Savanna validation Merkle state is inconsistent");
   }
   if (state.roots_.size() - 1U > std::numeric_limits<block_num_t>::max() - state.first_) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state, "Savanna validation block range overflows");
   }

   auto replay = state.prefix_;
   for (auto index = std::size_t{}; index < state.leaves_.size(); ++index) {
      if (state.leaves_[index].num != state.first_ + static_cast<block_num_t>(index)) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state,
                               "Savanna retained validation leaf is not contiguous",
                               forge::exceptions::ctx("leaf_index", index));
      }
      replay.append(leaf_digest(state.leaves_[index]));
      if (replay.root() != state.roots_[index]) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state, "Savanna retained validation root is inconsistent",
                               forge::exceptions::ctx("root_index", index));
      }
   }
   if (forge::raw::pack(replay) != forge::raw::pack(state.current_)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_validation_state,
                            "Savanna incremental validation tree is inconsistent");
   }
}

} // namespace forge::chain::savanna
