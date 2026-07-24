module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

module forge.chain.savanna.policy;

namespace forge::chain::savanna {
namespace {

void validate_structure(const finalizer_policy& policy) {
   if (policy.finalizers.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna finalizer policy cannot be empty");
   }

   auto total = std::uint64_t{};
   auto keys = std::set<forge::crypto::bls::public_key>{};
   for (const auto& finalizer : policy.finalizers) {
      if (finalizer.public_key == forge::crypto::bls::public_key{}) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna finalizer policy contains an identity public key");
      }
      if (finalizer.weight > std::numeric_limits<std::uint64_t>::max() - total) {
         FORGE_THROW_EXCEPTION(exceptions::policy_weight_overflow, "Savanna finalizer policy weight sum overflows");
      }
      total += finalizer.weight;
      if (!keys.insert(finalizer.public_key).second) {
         FORGE_THROW_EXCEPTION(exceptions::duplicate_finalizer,
                               "Savanna finalizer policy contains a duplicate public key");
      }
   }

   if (policy.threshold <= total / 2U || policy.threshold > total) {
      FORGE_THROW_EXCEPTION(
          exceptions::invalid_policy, "Savanna finalizer policy threshold must be a reachable strict majority",
          forge::exceptions::ctx("threshold", policy.threshold), forge::exceptions::ctx("total_weight", total));
   }
}

std::vector<forge::crypto::bls::proof_verified_public_key>
verify_keys(const std::vector<finalizer>& finalizers, std::span<const forge::crypto::bls::signature> proofs) {
   if (proofs.size() != finalizers.size()) {
      FORGE_THROW_EXCEPTION(
          exceptions::invalid_proof_of_possession, "Savanna finalizer proof count does not match the finalizer count",
          forge::exceptions::ctx("finalizers", finalizers.size()), forge::exceptions::ctx("proofs", proofs.size()));
   }

   auto keys = std::vector<forge::crypto::bls::proof_verified_public_key>{};
   keys.reserve(finalizers.size());
   for (auto index = std::size_t{}; index < finalizers.size(); ++index) {
      auto key = forge::crypto::bls::verify_proof_of_possession(finalizers[index].public_key, proofs[index]);
      if (!key) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_proof_of_possession,
                               "Savanna finalizer proof of possession is invalid",
                               forge::exceptions::ctx("finalizer_index", index));
      }
      keys.push_back(std::move(*key));
   }
   return keys;
}

template <typename Value>
void apply_removals(std::vector<Value>& source, std::span<const std::uint16_t> remove_indexes) {
   auto previous = std::optional<std::uint16_t>{};
   auto removed = std::size_t{};
   for (const auto index : remove_indexes) {
      if ((previous && index <= *previous) || index < removed ||
          static_cast<std::size_t>(index) - removed >= source.size()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna policy removal diff is not strictly ordered");
      }
      source.erase(source.begin() + static_cast<std::ptrdiff_t>(index - removed));
      previous = index;
      ++removed;
   }
}

template <typename Value>
void apply_insertions(std::vector<Value>& source, const std::vector<std::pair<std::uint16_t, Value>>& insert_indexes) {
   auto previous = std::optional<std::uint16_t>{};
   for (const auto& [index, value] : insert_indexes) {
      if ((previous && index <= *previous) || index > source.size()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna policy insertion diff is not strictly ordered");
      }
      source.insert(source.begin() + static_cast<std::ptrdiff_t>(index), value);
      previous = index;
   }
}

} // namespace

verified_finalizer_policy validate(finalizer_policy policy, std::span<const forge::crypto::bls::signature> proofs) {
   validate_structure(policy);
   auto keys = verify_keys(policy.finalizers, proofs);
   return verified_finalizer_policy{std::move(policy), std::move(keys)};
}

verified_finalizer_policy apply(const verified_finalizer_policy& source, const finalizer_policy_diff& difference,
                                std::span<const forge::crypto::bls::signature> inserted_proofs) {
   const auto& source_policy = source.get();
   if (source_policy.generation == std::numeric_limits<std::uint32_t>::max() ||
       difference.generation != source_policy.generation + 1U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna finalizer policy generation is not sequential");
   }

   auto inserted_finalizers = std::vector<finalizer>{};
   inserted_finalizers.reserve(difference.finalizers.insert_indexes.size());
   for (const auto& [index, value] : difference.finalizers.insert_indexes) {
      static_cast<void>(index);
      inserted_finalizers.push_back(value);
   }
   auto inserted_keys = verify_keys(inserted_finalizers, inserted_proofs);

   auto finalizers = source_policy.finalizers;
   const auto source_keys = source.verified_keys();
   auto keys = std::vector<forge::crypto::bls::proof_verified_public_key>{source_keys.begin(), source_keys.end()};
   apply_removals(finalizers, difference.finalizers.remove_indexes);
   apply_removals(keys, difference.finalizers.remove_indexes);
   apply_insertions(finalizers, difference.finalizers.insert_indexes);

   auto key_insertions = std::vector<std::pair<std::uint16_t, forge::crypto::bls::proof_verified_public_key>>{};
   key_insertions.reserve(difference.finalizers.insert_indexes.size());
   for (auto index = std::size_t{}; index < difference.finalizers.insert_indexes.size(); ++index) {
      key_insertions.emplace_back(difference.finalizers.insert_indexes[index].first, std::move(inserted_keys[index]));
   }
   apply_insertions(keys, key_insertions);

   auto result = finalizer_policy{
       .generation = difference.generation,
       .threshold = difference.threshold,
       .finalizers = std::move(finalizers),
   };
   validate_structure(result);
   return verified_finalizer_policy{std::move(result), std::move(keys)};
}

} // namespace forge::chain::savanna
