module;

#include <bn256/bn256.h>

#include <array>
#include <cstdint>
#include <span>
#include <utility>

module forge.crypto.bn256;

namespace forge::crypto::bn256 {

std::int32_t add(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right,
                 std::span<std::uint8_t> result) noexcept {
   if (left.size() != 64U || right.size() != 64U || result.size() < 64U) {
      return -1;
   }
   return ::bn256::g1_add(std::span<const std::uint8_t, 64>{left.data(), 64U},
                          std::span<const std::uint8_t, 64>{right.data(), 64U},
                          std::span<std::uint8_t, 64>{result.data(), 64U});
}

std::int32_t multiply(std::span<const std::uint8_t> point, std::span<const std::uint8_t> scalar,
                      std::span<std::uint8_t> result) noexcept {
   if (point.size() != 64U || scalar.size() != 32U || result.size() < 64U) {
      return -1;
   }
   return ::bn256::g1_scalar_mul(std::span<const std::uint8_t, 64>{point.data(), 64U},
                                 std::span<const std::uint8_t, 32>{scalar.data(), 32U},
                                 std::span<std::uint8_t, 64>{result.data(), 64U});
}

std::int32_t pairing_check(std::span<const std::uint8_t> pairs, yield_function yield) {
   if (!yield) {
      yield = [] {};
   }
   return ::bn256::pairing_check(pairs, std::move(yield));
}

} // namespace forge::crypto::bn256
