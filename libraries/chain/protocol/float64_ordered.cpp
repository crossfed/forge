module;

#include <forge/exceptions/macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

module forge.chain.protocol.float64;

import :value;
import forge.chain.protocol.exceptions;
import forge.chain.protocol.fixed_key;

namespace forge::chain::protocol {

fixed_key<8> ordered_key(float64 value) {
   if (is_nan(value)) {
      FORGE_THROW_EXCEPTION(exceptions::unordered_value, "NaN cannot be used as a float64 secondary index key");
   }

   const auto sortable = detail::sortable_float64_bits(value.bits);
   auto bytes = std::array<std::uint8_t, 8>{};
   for (auto index = std::size_t{}; index < bytes.size(); ++index) {
      const auto shift = static_cast<unsigned>((bytes.size() - index - 1U) * 8U);
      bytes[index] = static_cast<std::uint8_t>((sortable >> shift) & 0xffU);
   }
   return fixed_key<8>{bytes};
}

} // namespace forge::chain::protocol
