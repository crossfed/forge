module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

export module forge.contract.crypto_ext;

export import forge.contract.crypto;

import forge.contract.intrinsics;

export namespace forge::contract {

template <std::size_t Size = 32U> struct ec_point {
   std::vector<char> x;
   std::vector<char> y;

   ec_point() : x(Size), y(Size) {}
   ec_point(std::vector<char> x_value, std::vector<char> y_value) : x(std::move(x_value)), y(std::move(y_value)) {
      check(x.size() == y.size(), "x's size must be equal to y's");
      check(x.size() == Size, "point size must match");
   }

   explicit ec_point(const std::vector<char>& value)
       : x(value.begin(), value.begin() + std::min(value.size(), Size)),
         y(value.begin() + std::min(value.size(), Size), value.end()) {
      check(value.size() == Size * 2U, "point size must match");
   }

   [[nodiscard]] std::vector<char> serialized() const {
      auto result = x;
      result.insert(result.end(), y.begin(), y.end());
      return result;
   }
};

template <std::size_t Size = 32U> struct ec_point_view {
   const char* x = nullptr;
   const char* y = nullptr;
   std::uint32_t size = Size;

   ec_point_view(const char* x_value, std::uint32_t x_size, const char* y_value, std::uint32_t y_size)
       : x(x_value), y(y_value), size(x_size) {
      check(x_size == y_size, "x's size must be equal to y's");
      check(size == Size, "point size must match");
   }

   explicit ec_point_view(const std::vector<char>& value) : x(value.data()), y(value.data() + Size), size(Size) {
      check(value.size() == 2U * Size, "point size must match");
   }

   explicit ec_point_view(const ec_point<Size>& value) : x(value.x.data()), y(value.y.data()), size(Size) {}

   [[nodiscard]] std::vector<char> serialized() const {
      auto result = std::vector<char>(x, x + size);
      result.insert(result.end(), y, y + size);
      return result;
   }
};

inline constexpr std::size_t g1_coordinate_size = 32U;
inline constexpr std::size_t g2_coordinate_size = 64U;
using g1_point = ec_point<g1_coordinate_size>;
using g2_point = ec_point<g2_coordinate_size>;
using g1_point_view = ec_point_view<g1_coordinate_size>;
using g2_point_view = ec_point_view<g2_coordinate_size>;
using bigint = std::vector<char>;

[[nodiscard]] std::int32_t alt_bn128_add(const char* first, std::uint32_t first_size, const char* second,
                                         std::uint32_t second_size, char* result, std::uint32_t result_size);

template <typename First, typename Second>
[[nodiscard]] g1_point alt_bn128_add(const First& first, const Second& second) {
   const auto first_bytes = first.serialized();
   const auto second_bytes = second.serialized();
   auto result = g1_point{};
   auto output = result.serialized();
   check(::forge::contract::internal::alt_bn128_add(first_bytes.data(), first_bytes.size(), second_bytes.data(),
                                                    second_bytes.size(), output.data(), output.size()) == 0,
         "internal_use_do_not_use::alt_bn128_add failed");
   result.x.assign(output.begin(), output.begin() + g1_coordinate_size);
   result.y.assign(output.begin() + g1_coordinate_size, output.end());
   return result;
}

[[nodiscard]] std::int32_t alt_bn128_mul(const char* point, std::uint32_t point_size, const char* scalar,
                                         std::uint32_t scalar_size, char* result, std::uint32_t result_size);

template <typename Point> [[nodiscard]] g1_point alt_bn128_mul(const Point& point, const bigint& scalar) {
   const auto point_bytes = point.serialized();
   auto output = std::vector<char>(2U * g1_coordinate_size);
   check(::forge::contract::internal::alt_bn128_mul(point_bytes.data(), point_bytes.size(), scalar.data(),
                                                    scalar.size(), output.data(), output.size()) == 0,
         "internal_use_do_not_use::alt_bn128_mul failed");
   return g1_point{{output.begin(), output.begin() + g1_coordinate_size},
                   {output.begin() + g1_coordinate_size, output.end()}};
}

[[nodiscard]] std::int32_t alt_bn128_pair(const char* pairs, std::uint32_t size);

template <typename G1, typename G2>
[[nodiscard]] std::int32_t alt_bn128_pair(const std::vector<std::pair<G1, G2>>& pairs) {
   auto bytes = std::vector<char>{};
   bytes.reserve(pairs.size() * (2U * g1_coordinate_size + 2U * g2_coordinate_size));
   for (const auto& [first, second] : pairs) {
      const auto first_bytes = first.serialized();
      const auto second_bytes = second.serialized();
      bytes.insert(bytes.end(), first_bytes.begin(), first_bytes.end());
      bytes.insert(bytes.end(), second_bytes.begin(), second_bytes.end());
   }
   return ::forge::contract::internal::alt_bn128_pair(bytes.data(), bytes.size());
}

[[nodiscard]] std::int32_t mod_exp(const char* base, std::uint32_t base_size, const char* exponent,
                                   std::uint32_t exponent_size, const char* modulus, std::uint32_t modulus_size,
                                   char* result, std::uint32_t result_size);
[[nodiscard]] std::int32_t mod_exp(const bigint& base, const bigint& exponent, const bigint& modulus, bigint& result);

inline constexpr std::size_t blake2f_result_size = 64U;

[[nodiscard]] std::int32_t blake2_f(std::uint32_t rounds, const char* state, std::uint32_t state_size,
                                    const char* message, std::uint32_t message_size, const char* offset0,
                                    std::uint32_t offset0_size, const char* offset1, std::uint32_t offset1_size,
                                    std::int32_t final, char* result, std::uint32_t result_size);
[[nodiscard]] std::int32_t blake2_f(std::uint32_t rounds, const std::vector<char>& state,
                                    const std::vector<char>& message, const std::vector<char>& offset0,
                                    const std::vector<char>& offset1, bool final, std::vector<char>& result);
[[nodiscard]] checksum256 sha3(const char* data, std::uint32_t size);
void assert_sha3(const char* data, std::uint32_t size, const checksum256& expected);
[[nodiscard]] checksum256 keccak(const char* data, std::uint32_t size);
void assert_keccak(const char* data, std::uint32_t size, const checksum256& expected);
[[nodiscard]] std::int32_t k1_recover(const char* signature, std::uint32_t signature_size, const char* digest,
                                      std::uint32_t digest_size, char* public_key, std::uint32_t public_key_size);

} // namespace forge::contract
