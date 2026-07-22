module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstdint>
#include <limits>
#include <vector>

module forge.contract.producer_schedule;

import forge.contract.intrinsics;
import forge.raw.codec;

namespace forge::contract {

bool is_valid(const block_signing_authority_v0& authority) {
   auto total = std::uint32_t{};
   auto keys = std::vector<std::vector<std::uint8_t>>{};
   for (const auto& value : authority.keys) {
      total = std::numeric_limits<std::uint32_t>::max() - total <= value.weight
                  ? std::numeric_limits<std::uint32_t>::max()
                  : total + value.weight;
      const auto encoded = forge::raw::pack(value.key);
      for (const auto& key : keys) {
         if (key == encoded) {
            return false;
         }
      }
      keys.push_back(encoded);
   }
   return keys.size() == authority.keys.size() && authority.threshold != 0U && total >= authority.threshold;
}

std::vector<chain::protocol::name> get_active_producers() {
   const auto bytes = internal::get_active_producers(nullptr, 0U);
   check(bytes % sizeof(std::uint64_t) == 0U, "active producer payload has invalid size");
   auto result = std::vector<chain::protocol::name>(bytes / sizeof(std::uint64_t));
   if (bytes != 0U) {
      check(internal::get_active_producers(reinterpret_cast<std::uint64_t*>(result.data()), bytes) == bytes,
            "failed to read active producers");
   }
   return result;
}

} // namespace forge::contract
