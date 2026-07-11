module;

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

module forge.chain.merkle;

import forge.raw.raw;

namespace forge::chain {
namespace {

digest hash_pair(const digest& left, const digest& right) {
   auto encoder = digest::encoder{};
   forge::raw::pack(encoder, left);
   forge::raw::pack(encoder, right);
   return encoder.result();
}

digest calculate_power_of_two_root(std::span<const digest> leaves) {
   if (leaves.size() == 1U) {
      return leaves.front();
   }

   const auto midpoint = leaves.size() / 2U;
   return hash_pair(calculate_power_of_two_root(leaves.first(midpoint)),
                    calculate_power_of_two_root(leaves.subspan(midpoint)));
}

} // namespace

digest calculate_merkle_root(std::span<const digest> leaves) {
   if (leaves.empty()) {
      return {};
   }
   if (leaves.size() == 1U) {
      return leaves.front();
   }

   const auto midpoint = std::bit_floor(leaves.size());
   if (midpoint == leaves.size()) {
      return calculate_power_of_two_root(leaves);
   }

   return hash_pair(calculate_power_of_two_root(leaves.first(midpoint)),
                    calculate_merkle_root(leaves.subspan(midpoint)));
}

void incremental_merkle_tree::append(const digest& leaf) {
   if (mask_ == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error{"incremental merkle tree leaf count overflow"};
   }

   auto trees = trees_;
   auto root = leaf;
   auto rank = std::size_t{0};
   while ((mask_ & (std::uint64_t{1} << rank)) != 0U) {
      root = hash_pair(trees.back(), root);
      trees.pop_back();
      ++rank;
   }

   trees.push_back(root);
   trees_ = std::move(trees);
   ++mask_;
}

digest incremental_merkle_tree::root() const {
   if (trees_.empty()) {
      return {};
   }

   auto result = trees_.back();
   for (auto index = trees_.size() - 1U; index > 0U; --index) {
      result = hash_pair(trees_[index - 1U], result);
   }
   return result;
}

std::uint64_t incremental_merkle_tree::size() const noexcept {
   return mask_;
}

bool incremental_merkle_tree::empty() const noexcept {
   return mask_ == 0U;
}

} // namespace forge::chain
