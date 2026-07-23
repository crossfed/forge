module;

#include "limits.hpp"
#include <cstdint>
#include <optional>

module product.chain.protocol;

namespace product::chain {

std::optional<std::uint64_t> checked_add(std::uint64_t left, std::uint64_t right) {
   if (left > details::maximum_value - right) {
      return std::nullopt;
   }
   return left + right;
}

} // namespace product::chain
