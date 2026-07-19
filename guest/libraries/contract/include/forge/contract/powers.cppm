module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

export module forge.contract.powers;

import forge.contract.intrinsics;

export namespace forge::contract {

namespace detail {

template <typename T, T Base> consteval std::size_t power_count() {
   static_assert(std::is_unsigned_v<T> && !std::is_same_v<T, bool>);
   static_assert(Base > 1U);
   auto value = T{1};
   auto count = std::size_t{1};
   while (value <= std::numeric_limits<T>::max() / Base) {
      value *= Base;
      ++count;
   }
   return count;
}

template <typename T, T Base> consteval auto make_powers() {
   auto result = std::array<T, power_count<T, Base>()>{};
   auto value = T{1};
   for (auto& item : result) {
      item = value;
      if (&item != &result.back()) {
         value *= Base;
      }
   }
   return result;
}

} // namespace detail

template <std::uint8_t Base, typename T = std::uint64_t>
inline constexpr auto powers_of_base = detail::make_powers<T, static_cast<T>(Base)>();

template <std::uint8_t Base, typename T = std::uint64_t> [[nodiscard]] constexpr T pow(std::uint8_t exponent) {
   const auto& values = powers_of_base<Base, T>;
   check(exponent < values.size(), "overflow");
   return values[exponent];
}

} // namespace forge::contract
