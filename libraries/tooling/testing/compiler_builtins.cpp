module;

#include <forge/exceptions/macros.hpp>

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>

#include <softfloat.hpp>

module forge.tooling.testing.host;

import forge.tooling.testing.exceptions;
import forge.tooling.testing.schema;
import forge.vm.wasm.interpret.host_function;

#include "details/compiler_builtins.hxx"

namespace forge::tooling::testing {

namespace {

using signed_128 = __int128;
using unsigned_128 = unsigned __int128;

constexpr auto signed_min_bits = unsigned_128{1} << 127U;
constexpr auto signed_max_bits = signed_min_bits - 1U;

[[nodiscard]] constexpr unsigned_128 combine(std::uint64_t low, std::uint64_t high) noexcept {
   return (static_cast<unsigned_128>(high) << 64U) | low;
}

[[nodiscard]] constexpr signed_128 as_signed(unsigned_128 value) noexcept {
   return std::bit_cast<signed_128>(value);
}

[[nodiscard]] constexpr float128_t as_softfloat(std::uint64_t low, std::uint64_t high) noexcept {
   return float128_t{{low, high}};
}

[[nodiscard]] constexpr float128 from_softfloat(float128_t value) noexcept {
   return float128{{value.v[0], value.v[1]}};
}

[[noreturn]] void divide_by_zero() {
   FORGE_THROW_EXCEPTION(exceptions::assertion_failure, "divide by zero");
}

template <typename Rep>
[[nodiscard]] signed_128 fix_signed(Rep source, unsigned significand_bits, int exponent_bias) noexcept {
   const auto representation = static_cast<unsigned_128>(source);
   const auto sign_bit = unsigned_128{1} << (sizeof(Rep) * 8U - 1U);
   const auto absolute = representation & (sign_bit - 1U);
   const auto exponent = static_cast<int>(absolute >> significand_bits) - exponent_bias;
   const auto significand_mask = (unsigned_128{1} << significand_bits) - 1U;
   const auto significand = (absolute & significand_mask) | (unsigned_128{1} << significand_bits);

   if (exponent < 0) {
      return 0;
   }
   if (static_cast<unsigned>(exponent) >= 128U) {
      return as_signed((representation & sign_bit) == 0U ? signed_max_bits : signed_min_bits);
   }

   const auto magnitude = exponent < static_cast<int>(significand_bits)
                              ? significand >> (significand_bits - static_cast<unsigned>(exponent))
                              : significand << (static_cast<unsigned>(exponent) - significand_bits);
   return as_signed((representation & sign_bit) == 0U ? magnitude : (~magnitude + 1U));
}

template <typename Rep>
[[nodiscard]] unsigned_128 fix_unsigned(Rep source, unsigned significand_bits, int exponent_bias) noexcept {
   const auto representation = static_cast<unsigned_128>(source);
   const auto sign_bit = unsigned_128{1} << (sizeof(Rep) * 8U - 1U);
   const auto absolute = representation & (sign_bit - 1U);
   const auto exponent = static_cast<int>(absolute >> significand_bits) - exponent_bias;
   const auto significand_mask = (unsigned_128{1} << significand_bits) - 1U;
   const auto significand = (absolute & significand_mask) | (unsigned_128{1} << significand_bits);

   if ((representation & sign_bit) != 0U || exponent < 0) {
      return 0;
   }
   if (static_cast<unsigned>(exponent) >= 128U) {
      return ~unsigned_128{0};
   }
   return exponent < static_cast<int>(significand_bits)
              ? significand >> (significand_bits - static_cast<unsigned>(exponent))
              : significand << (static_cast<unsigned>(exponent) - significand_bits);
}

[[nodiscard]] unsigned bit_width(unsigned_128 value) noexcept {
   const auto high = static_cast<std::uint64_t>(value >> 64U);
   if (high != 0U) {
      return 128U - static_cast<unsigned>(std::countl_zero(high));
   }
   const auto low = static_cast<std::uint64_t>(value);
   return low == 0U ? 0U : 64U - static_cast<unsigned>(std::countl_zero(low));
}

[[nodiscard]] double uint128_to_double(unsigned_128 value, bool negative) noexcept {
   if (value == 0U) {
      return 0.0;
   }

   constexpr auto mantissa_digits = 53U;
   auto significant_digits = bit_width(value);
   auto exponent = significant_digits - 1U;
   if (significant_digits > mantissa_digits) {
      if (significant_digits == mantissa_digits + 1U) {
         value <<= 1U;
      } else if (significant_digits > mantissa_digits + 2U) {
         const auto shift = significant_digits - (mantissa_digits + 2U);
         const auto discarded = value & ((unsigned_128{1} << shift) - 1U);
         value = (value >> shift) | static_cast<unsigned_128>(discarded != 0U);
      }
      value |= static_cast<unsigned_128>((value & 4U) != 0U);
      ++value;
      value >>= 2U;
      if ((value & (unsigned_128{1} << mantissa_digits)) != 0U) {
         value >>= 1U;
         ++exponent;
      }
   } else {
      value <<= mantissa_digits - significant_digits;
   }

   const auto sign = negative ? std::uint64_t{1} << 63U : 0U;
   const auto biased_exponent = static_cast<std::uint64_t>(exponent + 1023U) << 52U;
   const auto mantissa = static_cast<std::uint64_t>(value) & ((std::uint64_t{1} << 52U) - 1U);
   return std::bit_cast<double>(sign | biased_exponent | mantissa);
}

[[nodiscard]] signed_128 fix_f128_signed(float128_t value) noexcept {
   return fix_signed(combine(value.v[0], value.v[1]), 112U, 16383);
}

[[nodiscard]] unsigned_128 fix_f128_unsigned(float128_t value) noexcept {
   return fix_unsigned(combine(value.v[0], value.v[1]), 112U, 16383);
}

[[nodiscard]] std::int32_t compare_f128(float128_t left, float128_t right, std::int32_t unordered) noexcept {
   if (!f128_eq(left, left) || !f128_eq(right, right)) {
      return unordered;
   }
   if (f128_lt(left, right)) {
      return -1;
   }
   return f128_eq(left, right) ? 0 : 1;
}

} // namespace

void compiler_builtins::__ashlti3(int128_output result, std::uint64_t low, std::uint64_t high,
                                  std::uint32_t shift) const {
   const auto value = combine(low, high);
   *result = as_signed(shift < 128U ? value << shift : 0U);
}

void compiler_builtins::__ashrti3(int128_output result, std::uint64_t low, std::uint64_t high,
                                  std::uint32_t shift) const {
   const auto value = combine(low, high);
   const auto negative = (value & signed_min_bits) != 0U;
   if (shift >= 128U) {
      *result = negative ? -1 : 0;
      return;
   }
   if (shift == 0U || !negative) {
      *result = as_signed(value >> shift);
      return;
   }
   *result = as_signed((value >> shift) | (~unsigned_128{0} << (128U - shift)));
}

void compiler_builtins::__lshlti3(int128_output result, std::uint64_t low, std::uint64_t high,
                                  std::uint32_t shift) const {
   const auto value = combine(low, high);
   *result = as_signed(shift < 128U ? value << shift : 0U);
}

void compiler_builtins::__lshrti3(int128_output result, std::uint64_t low, std::uint64_t high,
                                  std::uint32_t shift) const {
   *result = as_signed(shift < 128U ? combine(low, high) >> shift : 0U);
}

void compiler_builtins::__divti3(int128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                 std::uint64_t high_b) const {
   const auto left = as_signed(combine(low_a, high_a));
   const auto right = as_signed(combine(low_b, high_b));
   if (right == 0) {
      divide_by_zero();
   }
   if (std::bit_cast<unsigned_128>(left) == signed_min_bits && right == -1) {
      *result = left;
      return;
   }
   *result = left / right;
}

void compiler_builtins::__udivti3(uint128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                  std::uint64_t high_b) const {
   const auto divisor = combine(low_b, high_b);
   if (divisor == 0U) {
      divide_by_zero();
   }
   *result = combine(low_a, high_a) / divisor;
}

void compiler_builtins::__multi3(uint128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                 std::uint64_t high_b) const {
   *result = combine(low_a, high_a) * combine(low_b, high_b);
}

void compiler_builtins::__modti3(int128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                 std::uint64_t high_b) const {
   const auto left = as_signed(combine(low_a, high_a));
   const auto right = as_signed(combine(low_b, high_b));
   if (right == 0) {
      divide_by_zero();
   }
   if (std::bit_cast<unsigned_128>(left) == signed_min_bits && right == -1) {
      *result = 0;
      return;
   }
   *result = left % right;
}

void compiler_builtins::__umodti3(uint128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                  std::uint64_t high_b) const {
   const auto divisor = combine(low_b, high_b);
   if (divisor == 0U) {
      divide_by_zero();
   }
   *result = combine(low_a, high_a) % divisor;
}

void compiler_builtins::__addtf3(float128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                 std::uint64_t high_b) const {
   *result = from_softfloat(f128_add(as_softfloat(low_a, high_a), as_softfloat(low_b, high_b)));
}

void compiler_builtins::__subtf3(float128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                 std::uint64_t high_b) const {
   *result = from_softfloat(f128_sub(as_softfloat(low_a, high_a), as_softfloat(low_b, high_b)));
}

void compiler_builtins::__multf3(float128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                 std::uint64_t high_b) const {
   *result = from_softfloat(f128_mul(as_softfloat(low_a, high_a), as_softfloat(low_b, high_b)));
}

void compiler_builtins::__divtf3(float128_output result, std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                 std::uint64_t high_b) const {
   *result = from_softfloat(f128_div(as_softfloat(low_a, high_a), as_softfloat(low_b, high_b)));
}

void compiler_builtins::__negtf2(float128_output result, std::uint64_t low, std::uint64_t high) const {
   *result = float128{{low, high ^ (std::uint64_t{1} << 63U)}};
}

void compiler_builtins::__extendsftf2(float128_output result, float value) const {
   *result = from_softfloat(f32_to_f128(to_softfloat32(value)));
}

void compiler_builtins::__extenddftf2(float128_output result, double value) const {
   *result = from_softfloat(f64_to_f128(to_softfloat64(value)));
}

double compiler_builtins::__trunctfdf2(std::uint64_t low, std::uint64_t high) const {
   return from_softfloat64(f128_to_f64(as_softfloat(low, high)));
}

float compiler_builtins::__trunctfsf2(std::uint64_t low, std::uint64_t high) const {
   return from_softfloat32(f128_to_f32(as_softfloat(low, high)));
}

std::int32_t compiler_builtins::__fixtfsi(std::uint64_t low, std::uint64_t high) const {
   return f128_to_i32(as_softfloat(low, high), 0, false);
}

std::int64_t compiler_builtins::__fixtfdi(std::uint64_t low, std::uint64_t high) const {
   return f128_to_i64(as_softfloat(low, high), 0, false);
}

void compiler_builtins::__fixtfti(int128_output result, std::uint64_t low, std::uint64_t high) const {
   *result = fix_f128_signed(as_softfloat(low, high));
}

std::uint32_t compiler_builtins::__fixunstfsi(std::uint64_t low, std::uint64_t high) const {
   return f128_to_ui32(as_softfloat(low, high), 0, false);
}

std::uint64_t compiler_builtins::__fixunstfdi(std::uint64_t low, std::uint64_t high) const {
   return f128_to_ui64(as_softfloat(low, high), 0, false);
}

void compiler_builtins::__fixunstfti(uint128_output result, std::uint64_t low, std::uint64_t high) const {
   *result = fix_f128_unsigned(as_softfloat(low, high));
}

void compiler_builtins::__fixsfti(int128_output result, float value) const {
   *result = fix_signed(std::bit_cast<std::uint32_t>(value), 23U, 127);
}

void compiler_builtins::__fixdfti(int128_output result, double value) const {
   *result = fix_signed(std::bit_cast<std::uint64_t>(value), 52U, 1023);
}

void compiler_builtins::__fixunssfti(uint128_output result, float value) const {
   *result = fix_unsigned(std::bit_cast<std::uint32_t>(value), 23U, 127);
}

void compiler_builtins::__fixunsdfti(uint128_output result, double value) const {
   *result = fix_unsigned(std::bit_cast<std::uint64_t>(value), 52U, 1023);
}

double compiler_builtins::__floatsidf(std::int32_t value) const {
   return from_softfloat64(i32_to_f64(value));
}

void compiler_builtins::__floatsitf(float128_output result, std::int32_t value) const {
   *result = from_softfloat(i32_to_f128(value));
}

void compiler_builtins::__floatditf(float128_output result, std::uint64_t value) const {
   *result = from_softfloat(i64_to_f128(static_cast<std::int64_t>(value)));
}

void compiler_builtins::__floatunsitf(float128_output result, std::uint32_t value) const {
   *result = from_softfloat(ui32_to_f128(value));
}

void compiler_builtins::__floatunditf(float128_output result, std::uint64_t value) const {
   *result = from_softfloat(ui64_to_f128(value));
}

double compiler_builtins::__floattidf(std::uint64_t low, std::uint64_t high) const {
   const auto bits = combine(low, high);
   const auto negative = (bits & signed_min_bits) != 0U;
   const auto magnitude = negative ? ~bits + 1U : bits;
   return uint128_to_double(magnitude, negative);
}

double compiler_builtins::__floatuntidf(std::uint64_t low, std::uint64_t high) const {
   return uint128_to_double(combine(low, high), false);
}

std::int32_t compiler_builtins::__cmptf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                         std::uint64_t high_b) const {
   return compare_f128(as_softfloat(low_a, high_a), as_softfloat(low_b, high_b), 1);
}

std::int32_t compiler_builtins::__eqtf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                        std::uint64_t high_b) const {
   return compare_f128(as_softfloat(low_a, high_a), as_softfloat(low_b, high_b), 1);
}

std::int32_t compiler_builtins::__netf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                        std::uint64_t high_b) const {
   return compare_f128(as_softfloat(low_a, high_a), as_softfloat(low_b, high_b), 1);
}

std::int32_t compiler_builtins::__getf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                        std::uint64_t high_b) const {
   return compare_f128(as_softfloat(low_a, high_a), as_softfloat(low_b, high_b), -1);
}

std::int32_t compiler_builtins::__gttf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                        std::uint64_t high_b) const {
   return __getf2(low_a, high_a, low_b, high_b);
}

std::int32_t compiler_builtins::__letf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                        std::uint64_t high_b) const {
   return compare_f128(as_softfloat(low_a, high_a), as_softfloat(low_b, high_b), 1);
}

std::int32_t compiler_builtins::__lttf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                        std::uint64_t high_b) const {
   return __letf2(low_a, high_a, low_b, high_b);
}

std::int32_t compiler_builtins::__unordtf2(std::uint64_t low_a, std::uint64_t high_a, std::uint64_t low_b,
                                           std::uint64_t high_b) const {
   const auto left = as_softfloat(low_a, high_a);
   const auto right = as_softfloat(low_b, high_b);
   return !f128_eq(left, left) || !f128_eq(right, right) ? 1 : 0;
}

} // namespace forge::tooling::testing
