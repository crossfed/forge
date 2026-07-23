module;

#include <forge/exceptions/macros.hpp>

#include <limits>
#include <span>
#include <vector>

module forge.chain.quorum.evaluate;

namespace forge::chain::quorum {
namespace {

void add_checked(std::uint64_t& value, std::uint64_t amount) {
   if (amount > std::numeric_limits<std::uint64_t>::max() - value) {
      FORGE_THROW_EXCEPTION(exceptions::weight_overflow, "quorum weight sum overflow");
   }
   value += amount;
}

} // namespace

result evaluate(std::uint64_t threshold, std::span<const std::uint64_t> weights,
                std::span<const std::uint32_t> signer_indices) {
   auto value = result{.threshold = threshold};
   for (const auto weight : weights) {
      add_checked(value.total_weight, weight);
   }

   auto selected = std::vector<bool>(weights.size(), false);
   for (const auto index : signer_indices) {
      if (index >= weights.size()) {
         FORGE_THROW_EXCEPTION(exceptions::signer_out_of_range, "quorum signer index is out of range",
                               forge::exceptions::ctx("index", index));
      }
      if (selected[index]) {
         FORGE_THROW_EXCEPTION(exceptions::duplicate_signer, "quorum signer index is duplicated",
                               forge::exceptions::ctx("index", index));
      }
      selected[index] = true;
      add_checked(value.signed_weight, weights[index]);
   }

   value.state = value.signed_weight >= threshold ? status::reached : status::insufficient;
   return value;
}

} // namespace forge::chain::quorum
