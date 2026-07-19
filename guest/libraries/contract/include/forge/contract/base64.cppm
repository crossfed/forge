module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

export module forge.contract.base64;

import forge.contract.intrinsics;

export namespace forge::contract {

namespace detail {

inline constexpr std::string_view base64_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline constexpr std::string_view base64url_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

[[nodiscard]] inline std::string encode_base64(std::string_view input, bool url) {
   const auto alphabet = url ? base64url_alphabet : base64_alphabet;
   auto output = std::string{};
   output.reserve(((input.size() + 2U) / 3U) * 4U);
   for (auto offset = std::size_t{}; offset < input.size(); offset += 3U) {
      const auto remaining = input.size() - offset;
      const auto first = static_cast<std::uint8_t>(input[offset]);
      const auto second = remaining > 1U ? static_cast<std::uint8_t>(input[offset + 1U]) : 0U;
      const auto third = remaining > 2U ? static_cast<std::uint8_t>(input[offset + 2U]) : 0U;
      const auto value = (static_cast<std::uint32_t>(first) << 16U) |
                         (static_cast<std::uint32_t>(second) << 8U) | static_cast<std::uint32_t>(third);
      output.push_back(alphabet[(value >> 18U) & 0x3fU]);
      output.push_back(alphabet[(value >> 12U) & 0x3fU]);
      if (remaining > 1U) {
         output.push_back(alphabet[(value >> 6U) & 0x3fU]);
      } else if (!url) {
         output.push_back('=');
      }
      if (remaining > 2U) {
         output.push_back(alphabet[value & 0x3fU]);
      } else if (!url) {
         output.push_back('=');
      }
   }
   return output;
}

[[nodiscard]] inline std::uint8_t decode_base64_character(char character, bool url) {
   const auto alphabet = url ? base64url_alphabet : base64_alphabet;
   const auto position = alphabet.find(character);
   check(position != std::string_view::npos, "invalid base64 character");
   return static_cast<std::uint8_t>(position);
}

[[nodiscard]] inline std::string decode_base64(std::string_view input, bool url) {
   auto normalized = std::string{};
   normalized.reserve(input.size() + 3U);
   for (const auto character : input) {
      if (character != '\r' && character != '\n') {
         normalized.push_back(character);
      }
   }
   check(url || normalized.size() % 4U == 0U, "invalid base64 length");
   check(normalized.size() % 4U != 1U, "invalid base64 length");
   while (url && normalized.size() % 4U != 0U) {
      normalized.push_back('=');
   }

   auto output = std::string{};
   output.reserve((normalized.size() / 4U) * 3U);
   for (auto offset = std::size_t{}; offset < normalized.size(); offset += 4U) {
      const auto third_padding = normalized[offset + 2U] == '=';
      const auto fourth_padding = normalized[offset + 3U] == '=';
      check(!third_padding || fourth_padding, "invalid base64 padding");
      check(offset + 4U == normalized.size() || (!third_padding && !fourth_padding), "invalid base64 padding");
      const auto first = decode_base64_character(normalized[offset], url);
      const auto second = decode_base64_character(normalized[offset + 1U], url);
      const auto third = third_padding ? 0U : decode_base64_character(normalized[offset + 2U], url);
      const auto fourth = fourth_padding ? 0U : decode_base64_character(normalized[offset + 3U], url);
      const auto value = (static_cast<std::uint32_t>(first) << 18U) |
                         (static_cast<std::uint32_t>(second) << 12U) |
                         (static_cast<std::uint32_t>(third) << 6U) | static_cast<std::uint32_t>(fourth);
      output.push_back(static_cast<char>((value >> 16U) & 0xffU));
      if (!third_padding) {
         output.push_back(static_cast<char>((value >> 8U) & 0xffU));
      }
      if (!fourth_padding) {
         output.push_back(static_cast<char>(value & 0xffU));
      }
   }
   return output;
}

} // namespace detail

[[nodiscard]] inline std::string base64_encode(std::string_view value) {
   return detail::encode_base64(value, false);
}

[[nodiscard]] inline std::string base64_decode(std::string_view value) {
   return detail::decode_base64(value, false);
}

[[nodiscard]] inline std::string base64url_encode(std::string_view value) {
   return detail::encode_base64(value, true);
}

[[nodiscard]] inline std::string base64url_decode(std::string_view value) {
   return detail::decode_base64(value, true);
}

} // namespace forge::contract
