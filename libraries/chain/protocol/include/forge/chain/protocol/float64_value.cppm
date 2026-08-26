module;

#include <compare>
#include <cstdint>

export module forge.chain.protocol.float64:value;

namespace forge::chain::protocol::detail {

inline constexpr auto float64_sign_mask = std::uint64_t{0x8000000000000000ULL};
inline constexpr auto float64_exponent_mask = std::uint64_t{0x7ff0000000000000ULL};
inline constexpr auto float64_fraction_mask = std::uint64_t{0x000fffffffffffffULL};

[[nodiscard]] constexpr std::uint64_t normalize_float64_zero(std::uint64_t bits) noexcept {
   return (bits & ~float64_sign_mask) == 0U ? 0U : bits;
}

[[nodiscard]] constexpr std::uint64_t sortable_float64_bits(std::uint64_t bits) noexcept {
   const auto normalized = normalize_float64_zero(bits);
   return (normalized & float64_sign_mask) != 0U ? ~normalized : normalized ^ float64_sign_mask;
}

} // namespace forge::chain::protocol::detail

export namespace forge::chain::protocol {

struct float64 {
   std::uint64_t bits = 0;

   bool operator==(const float64&) const = default;
};

[[nodiscard]] constexpr bool is_nan(float64 value) noexcept {
   return (value.bits & detail::float64_exponent_mask) == detail::float64_exponent_mask &&
          (value.bits & detail::float64_fraction_mask) != 0U;
}

[[nodiscard]] constexpr std::partial_ordering compare(float64 left, float64 right) noexcept {
   if (is_nan(left) || is_nan(right)) {
      return std::partial_ordering::unordered;
   }

   const auto left_bits = detail::sortable_float64_bits(left.bits);
   const auto right_bits = detail::sortable_float64_bits(right.bits);
   if (left_bits < right_bits) {
      return std::partial_ordering::less;
   }
   if (left_bits > right_bits) {
      return std::partial_ordering::greater;
   }
   return std::partial_ordering::equivalent;
}

} // namespace forge::chain::protocol
