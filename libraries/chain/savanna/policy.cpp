module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <utility>
#include <vector>

module forge.chain.savanna.policy;

namespace forge::chain::savanna {
namespace {

std::vector<finalizer> apply_diff(std::vector<finalizer> source, const ordered_diff<finalizer>& difference) {
   auto previous = std::optional<std::uint16_t>{};
   auto removed = std::size_t{};
   for (const auto index : difference.remove_indexes) {
      if ((previous && index <= *previous) || index < removed ||
          static_cast<std::size_t>(index) - removed >= source.size()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna policy removal diff is not strictly ordered");
      }
      source.erase(source.begin() + static_cast<std::ptrdiff_t>(index - removed));
      previous = index;
      ++removed;
   }

   previous.reset();
   for (const auto& [index, value] : difference.insert_indexes) {
      if ((previous && index <= *previous) || index > source.size()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna policy insertion diff is not strictly ordered");
      }
      source.insert(source.begin() + static_cast<std::ptrdiff_t>(index), value);
      previous = index;
   }
   return source;
}

} // namespace

void validate(const finalizer_policy& policy) {
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

finalizer_policy apply(const finalizer_policy& source, const finalizer_policy_diff& difference) {
   validate(source);
   if (source.generation == std::numeric_limits<std::uint32_t>::max() ||
       difference.generation != source.generation + 1U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_policy, "Savanna finalizer policy generation is not sequential");
   }

   auto result = finalizer_policy{
       .generation = difference.generation,
       .threshold = difference.threshold,
       .finalizers = apply_diff(source.finalizers, difference.finalizers),
   };
   validate(result);
   return result;
}

} // namespace forge::chain::savanna
