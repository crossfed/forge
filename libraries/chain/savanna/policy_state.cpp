module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

module forge.chain.savanna.policy_state;

import forge.crypto.digest.sha256;
import forge.raw.raw;

namespace forge::chain::savanna {
namespace {

inline constexpr auto max_policy_entries = std::size_t{64U * 1024U};

template <typename Value> void apply_removals(std::vector<Value>& source, std::span<const std::uint16_t> indexes) {
   auto previous = std::optional<std::uint16_t>{};
   auto removed = std::size_t{};
   for (const auto index : indexes) {
      if ((previous && index <= *previous) || index < removed ||
          static_cast<std::size_t>(index) - removed >= source.size()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna policy removal diff is invalid");
      }
      source.erase(source.begin() + static_cast<std::ptrdiff_t>(index - removed));
      previous = index;
      ++removed;
   }
}

template <typename Value>
void apply_insertions(std::vector<Value>& source, const std::vector<std::pair<std::uint16_t, Value>>& insertions) {
   auto previous = std::optional<std::uint16_t>{};
   for (const auto& [index, value] : insertions) {
      if ((previous && index <= *previous) || index > source.size()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna policy insertion diff is invalid");
      }
      source.insert(source.begin() + static_cast<std::ptrdiff_t>(index), value);
      previous = index;
   }
}

template <typename Value>
std::vector<Value> apply_diff(std::vector<Value> source, const ordered_diff<Value>& difference) {
   apply_removals(source, difference.remove_indexes);
   apply_insertions(source, difference.insert_indexes);
   return source;
}

template <typename Value>
ordered_diff<Value> make_difference(const std::vector<Value>& source, const std::vector<Value>& target) {
   constexpr auto max_index = std::numeric_limits<std::uint16_t>::max();
   static_assert(max_policy_entries == static_cast<std::size_t>(max_index) + 1U);
   if ((!source.empty() && source.size() - 1U > max_index) || (!target.empty() && target.size() - 1U > max_index)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna policy exceeds ordered diff index range");
   }

   auto result = ordered_diff<Value>{};
   auto source_index = std::size_t{};
   auto target_index = std::size_t{};
   while (source_index < source.size() || target_index < target.size()) {
      if (source_index < source.size() && target_index < target.size()) {
         if (source[source_index] == target[target_index]) {
            ++source_index;
            ++target_index;
         } else if (source_index == source.size() - 1U && target_index == target.size() - 1U) {
            result.remove_indexes.push_back(static_cast<std::uint16_t>(source_index));
            result.insert_indexes.emplace_back(static_cast<std::uint16_t>(target_index), target[target_index]);
            ++source_index;
            ++target_index;
         } else if (source_index + 1U < source.size() && target_index + 1U < target.size() &&
                    source[source_index + 1U] == target[target_index + 1U]) {
            result.remove_indexes.push_back(static_cast<std::uint16_t>(source_index));
            result.insert_indexes.emplace_back(static_cast<std::uint16_t>(target_index), target[target_index]);
            ++source_index;
            ++target_index;
         } else if (target_index + 1U < target.size() && source[source_index] == target[target_index + 1U]) {
            result.insert_indexes.emplace_back(static_cast<std::uint16_t>(target_index), target[target_index]);
            ++target_index;
         } else {
            result.remove_indexes.push_back(static_cast<std::uint16_t>(source_index));
            ++source_index;
         }
      } else if (source_index < source.size()) {
         result.remove_indexes.push_back(static_cast<std::uint16_t>(source_index));
         ++source_index;
      } else {
         result.insert_indexes.emplace_back(static_cast<std::uint16_t>(target_index), target[target_index]);
         ++target_index;
      }
   }
   return result;
}

std::vector<forge::crypto::bls::signature>
apply_proofs(std::vector<forge::crypto::bls::signature> source, const finalizer_policy_diff& difference,
             std::span<const forge::crypto::bls::signature> inserted_proofs) {
   if (inserted_proofs.size() != difference.finalizers.insert_indexes.size()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_policy,
                            "Savanna finalizer proof count does not match policy insertions",
                            forge::exceptions::ctx("proofs", inserted_proofs.size()),
                            forge::exceptions::ctx("insertions", difference.finalizers.insert_indexes.size()));
   }
   apply_removals(source, difference.finalizers.remove_indexes);
   auto insertions = std::vector<std::pair<std::uint16_t, forge::crypto::bls::signature>>{};
   insertions.reserve(inserted_proofs.size());
   for (auto index = std::size_t{}; index < inserted_proofs.size(); ++index) {
      insertions.emplace_back(difference.finalizers.insert_indexes[index].first, inserted_proofs[index]);
   }
   apply_insertions(source, insertions);
   return source;
}

} // namespace

verified_finalizer_policy validate(const finalizer_policy_state& value) {
   const auto source = forge::crypto::digest::sha256::hash(forge::raw::pack(std::pair{value.policy, value.proofs}));
   if (value.verified && value.verified_source == source) {
      return *value.verified;
   }

   auto verified = forge::chain::savanna::validate(value.policy, value.proofs);
   value.verified_source = source;
   value.verified = verified;
   return verified;
}

finalizer_policy_state apply(const finalizer_policy_state& source, const finalizer_policy_diff& difference,
                             std::span<const forge::crypto::bls::signature> inserted_proofs) {
   const auto verified = forge::chain::savanna::apply(validate(source), difference, inserted_proofs);
   auto result = finalizer_policy_state{
       .policy = verified.get(),
       .proofs = apply_proofs(source.proofs, difference, inserted_proofs),
   };
   result.verified_source =
       forge::crypto::digest::sha256::hash(forge::raw::pack(std::pair{result.policy, result.proofs}));
   result.verified = verified;
   return result;
}

proposer_policy apply(const proposer_policy& source, const proposer_policy_diff& difference) {
   if (difference.version <= source.proposers.version) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna proposer policy diff does not advance version");
   }
   auto result = proposer_policy{
       .proposal_time = difference.proposal_time,
       .proposers = source.proposers,
   };
   result.proposers.version = difference.version;
   result.proposers.producers = apply_diff(source.proposers.producers, difference.producers);
   if (result.proposers.producers.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna proposer policy cannot be empty");
   }
   return result;
}

finalizer_policy_diff difference(const finalizer_policy_state& source, const finalizer_policy& target) {
   if (target.generation <= source.policy.generation) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna finalizer policy does not advance generation");
   }
   return {
       .generation = target.generation,
       .threshold = target.threshold,
       .finalizers = make_difference(source.policy.finalizers, target.finalizers),
   };
}

proposer_policy_diff difference(const proposer_policy& source,
                                const forge::chain::protocol::producer_authority_schedule& target,
                                block_timestamp proposal_time) {
   if (target.version <= source.proposers.version) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna proposer policy does not advance version");
   }
   return {
       .version = target.version,
       .proposal_time = proposal_time,
       .producers = make_difference(source.proposers.producers, target.producers),
   };
}

} // namespace forge::chain::savanna
