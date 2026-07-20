module;

#include <forge/exceptions/policy.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module forge.codec.hex;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.exceptions;
#endif

namespace forge::codec::hex {
namespace {

[[noreturn]] void fail_input(std::string_view message) {
   FORGE_POLICY_THROW_EXCEPTION(exceptions::invalid_input, message);
}

[[nodiscard]] std::uint8_t decode_character(char value) {
   if (value >= '0' && value <= '9') {
      return static_cast<std::uint8_t>(value - '0');
   }
   if (value >= 'a' && value <= 'f') {
      return static_cast<std::uint8_t>(value - 'a' + 10);
   }
   if (value >= 'A' && value <= 'F') {
      return static_cast<std::uint8_t>(value - 'A' + 10);
   }
   fail_input("invalid hex character");
}

} // namespace

namespace detail {

void require_integer_width(std::size_t width) {
   if (width > std::numeric_limits<std::size_t>::max() / 4U) {
      fail_input("hex width is too large");
   }
}

} // namespace detail

std::string encode(std::span<const std::uint8_t> input, letter_case letters) {
   if (input.size() > std::numeric_limits<std::size_t>::max() / 2U) {
      fail_input("hex input is too large");
   }
   const auto digits =
       letters == letter_case::lower ? std::string_view{"0123456789abcdef"} : std::string_view{"0123456789ABCDEF"};
   auto output = std::string{};
   output.reserve(input.size() * 2U);
   for (const auto byte : input) {
      output.push_back(digits[byte >> 4U]);
      output.push_back(digits[byte & 0x0fU]);
   }
   return output;
}

std::size_t decode(std::string_view input, std::span<std::uint8_t> output) {
   if (input.size() % 2U != 0U) {
      fail_input("hex input length must be even");
   }
   const auto required = input.size() / 2U;
   if (required > output.size()) {
      FORGE_POLICY_THROW_EXCEPTION(exceptions::insufficient_output, "hex output buffer is too small");
   }
   for (auto index = std::size_t{}; index < required; ++index) {
      output[index] = static_cast<std::uint8_t>((decode_character(input[index * 2U]) << 4U) |
                                                decode_character(input[index * 2U + 1U]));
   }
   return required;
}

std::vector<std::uint8_t> decode(std::string_view input) {
   auto output = std::vector<std::uint8_t>(input.size() / 2U);
   decode(input, output);
   return output;
}

} // namespace forge::codec::hex
