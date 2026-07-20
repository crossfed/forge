module;

#include <forge/exceptions/policy.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module forge.codec.base58;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.exceptions;
#endif

namespace forge::codec::base58 {
namespace {

// Copyright (c) 2014-present The Bitcoin Core developers.
// Distributed under the MIT software license.
constexpr auto alphabet = std::string_view{"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"};

[[noreturn]] void fail(std::string_view message) {
   FORGE_POLICY_THROW_EXCEPTION(exceptions::invalid_input, message);
}

[[nodiscard]] int character_value(char character) noexcept {
   const auto position = alphabet.find(character);
   return position == std::string_view::npos ? -1 : static_cast<int>(position);
}

[[nodiscard]] std::size_t scaled_capacity(std::size_t size, std::size_t numerator, std::size_t denominator) {
   if (size > (std::numeric_limits<std::size_t>::max() - 1U) / numerator) {
      fail("base58 input is too large");
   }
   return size * numerator / denominator + 1U;
}

} // namespace

std::string encode(std::span<const std::uint8_t> input) {
   auto zeroes = std::size_t{};
   while (zeroes < input.size() && input[zeroes] == 0U) {
      ++zeroes;
   }

   auto digits = std::vector<std::uint8_t>(scaled_capacity(input.size() - zeroes, 138U, 100U));
   auto length = std::size_t{};
   for (auto offset = zeroes; offset < input.size(); ++offset) {
      auto carry = static_cast<unsigned>(input[offset]);
      auto used = std::size_t{};
      for (auto iterator = digits.rbegin(); (carry != 0U || used < length) && iterator != digits.rend();
           ++iterator, ++used) {
         carry += 256U * *iterator;
         *iterator = static_cast<std::uint8_t>(carry % 58U);
         carry /= 58U;
      }
      length = used;
   }

   auto iterator = digits.begin() + static_cast<std::ptrdiff_t>(digits.size() - length);
   while (iterator != digits.end() && *iterator == 0U) {
      ++iterator;
   }

   auto output = std::string(zeroes, '1');
   output.reserve(zeroes + static_cast<std::size_t>(digits.end() - iterator));
   while (iterator != digits.end()) {
      output.push_back(alphabet[*iterator]);
      ++iterator;
   }
   return output;
}

std::vector<std::uint8_t> decode(std::string_view input) {
   auto zeroes = std::size_t{};
   while (zeroes < input.size() && input[zeroes] == '1') {
      ++zeroes;
   }

   auto bytes = std::vector<std::uint8_t>(scaled_capacity(input.size() - zeroes, 733U, 1000U));
   auto length = std::size_t{};
   for (auto offset = zeroes; offset < input.size(); ++offset) {
      const auto decoded = character_value(input[offset]);
      if (decoded < 0) {
         fail("encountered non-base58 character");
      }
      auto carry = static_cast<unsigned>(decoded);
      auto used = std::size_t{};
      for (auto iterator = bytes.rbegin(); (carry != 0U || used < length) && iterator != bytes.rend();
           ++iterator, ++used) {
         carry += 58U * *iterator;
         *iterator = static_cast<std::uint8_t>(carry % 256U);
         carry /= 256U;
      }
      length = used;
   }

   auto iterator = bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() - length);
   while (iterator != bytes.end() && *iterator == 0U) {
      ++iterator;
   }

   auto output = std::vector<std::uint8_t>(zeroes, 0U);
   output.insert(output.end(), iterator, bytes.end());
   return output;
}

} // namespace forge::codec::base58
