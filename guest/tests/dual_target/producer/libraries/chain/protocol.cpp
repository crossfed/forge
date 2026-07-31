module;

#include <cstdint>
#include <limits>
#include <optional>

module product.chain.protocol;

import product.chain.limits;

namespace product::chain {

std::optional<std::uint64_t>
checked_add(std::uint64_t left, std::uint64_t right) {
   if (!is_supported_size(1) ||
       left > std::numeric_limits<std::uint64_t>::max() - right) {
      return std::nullopt;
   }
   return left + right;
}

} // namespace product::chain
