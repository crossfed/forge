module;

#include <compare>

export module forge.chain.protocol.float128:value;

import forge.chain.protocol.values;

namespace forge::chain::protocol::detail {

inline constexpr auto float128_sign_mask = uint128_t{1U} << 127U;
inline constexpr auto float128_exponent_mask = uint128_t{0x7fffU} << 112U;
inline constexpr auto float128_fraction_mask = (uint128_t{1U} << 112U) - 1U;

[[nodiscard]] constexpr uint128_t normalize_float128_zero(uint128_t bits) noexcept {
   return (bits & ~float128_sign_mask) == 0U ? 0U : bits;
}

[[nodiscard]] constexpr uint128_t sortable_float128_bits(uint128_t bits) noexcept {
   const auto normalized = normalize_float128_zero(bits);
   return (normalized & float128_sign_mask) != 0U ? ~normalized : normalized ^ float128_sign_mask;
}

} // namespace forge::chain::protocol::detail

export namespace forge::chain::protocol {

struct float128 {
   uint128_t bits = 0;

   bool operator==(const float128&) const = default;
};

[[nodiscard]] constexpr bool is_nan(float128 value) noexcept {
   return (value.bits & detail::float128_exponent_mask) == detail::float128_exponent_mask &&
          (value.bits & detail::float128_fraction_mask) != 0U;
}

[[nodiscard]] constexpr std::partial_ordering compare(float128 left, float128 right) noexcept {
   if (is_nan(left) || is_nan(right)) {
      return std::partial_ordering::unordered;
   }

   const auto left_bits = detail::sortable_float128_bits(left.bits);
   const auto right_bits = detail::sortable_float128_bits(right.bits);
   if (left_bits < right_bits) {
      return std::partial_ordering::less;
   }
   if (left_bits > right_bits) {
      return std::partial_ordering::greater;
   }
   return std::partial_ordering::equivalent;
}

} // namespace forge::chain::protocol
