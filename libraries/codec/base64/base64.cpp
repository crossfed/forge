module;

#include <forge/exceptions/policy.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module forge.codec.base64;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.exceptions;
#endif

namespace forge::codec::base64 {
namespace {

constexpr auto standard_alphabet = std::string_view{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
constexpr auto url_alphabet = std::string_view{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"};

[[noreturn]] void fail(std::string_view message) {
   FORGE_POLICY_THROW_EXCEPTION(exceptions::invalid_input, message);
}

[[nodiscard]] constexpr bool is_ascii_whitespace(char value) noexcept {
   return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v';
}

[[nodiscard]] std::string normalize(std::string_view input, bool ignore_ascii_whitespace) {
   auto result = std::string{};
   result.reserve(input.size());
   for (const auto character : input) {
      if (is_ascii_whitespace(character)) {
         if (!ignore_ascii_whitespace) {
            fail("base64 input contains whitespace");
         }
         continue;
      }
      result.push_back(character);
   }
   return result;
}

[[nodiscard]] int decode_character(char value, alphabet characters) noexcept {
   if (value >= 'A' && value <= 'Z') {
      return value - 'A';
   }
   if (value >= 'a' && value <= 'z') {
      return value - 'a' + 26;
   }
   if (value >= '0' && value <= '9') {
      return value - '0' + 52;
   }
   if (characters == alphabet::standard) {
      if (value == '+') {
         return 62;
      }
      if (value == '/') {
         return 63;
      }
   } else {
      if (value == '-') {
         return 62;
      }
      if (value == '_') {
         return 63;
      }
   }
   return -1;
}

void append_wrapped(std::string& output, char value, std::size_t line_width, std::size_t& column) {
   if (line_width != 0U && column == line_width) {
      output.push_back('\n');
      column = 0U;
   }
   output.push_back(value);
   ++column;
}

} // namespace

std::string encode(std::span<const std::uint8_t> input, encode_options options) {
   const auto characters = options.characters == alphabet::standard ? standard_alphabet : url_alphabet;
   const auto groups = input.size() / 3U + static_cast<std::size_t>(input.size() % 3U != 0U);
   if (groups > std::numeric_limits<std::size_t>::max() / 4U) {
      fail("base64 input is too large");
   }
   const auto encoded_size = groups * 4U;
   const auto line_breaks =
       options.line_width == 0U || encoded_size == 0U ? 0U : (encoded_size - 1U) / options.line_width;
   if (line_breaks > std::numeric_limits<std::size_t>::max() - encoded_size) {
      fail("base64 output is too large");
   }
   auto output = std::string{};
   output.reserve(encoded_size + line_breaks);
   auto column = std::size_t{};

   for (auto offset = std::size_t{}; offset < input.size(); offset += 3U) {
      const auto first = input[offset];
      const auto second = offset + 1U < input.size() ? input[offset + 1U] : std::uint8_t{};
      const auto third = offset + 2U < input.size() ? input[offset + 2U] : std::uint8_t{};

      append_wrapped(output, characters[first >> 2U], options.line_width, column);
      append_wrapped(output, characters[((first & 0x03U) << 4U) | (second >> 4U)], options.line_width, column);
      if (offset + 1U < input.size()) {
         append_wrapped(output, characters[((second & 0x0fU) << 2U) | (third >> 6U)], options.line_width, column);
      } else if (options.pad == padding::include) {
         append_wrapped(output, '=', options.line_width, column);
      }
      if (offset + 2U < input.size()) {
         append_wrapped(output, characters[third & 0x3fU], options.line_width, column);
      } else if (options.pad == padding::include) {
         append_wrapped(output, '=', options.line_width, column);
      }
   }
   return output;
}

std::string encode(std::string_view input, encode_options options) {
   return encode(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(input.data()), input.size()},
                 options);
}

std::vector<std::uint8_t> decode(std::string_view input, decode_options options) {
   auto normalized_storage = std::string{};
   auto normalized = input;
   if (options.ignore_ascii_whitespace) {
      normalized_storage = normalize(input, true);
      normalized = normalized_storage;
   } else if (std::ranges::any_of(input, is_ascii_whitespace)) {
      fail("base64 input contains whitespace");
   }
   if (normalized.empty()) {
      return {};
   }

   auto padding_size = std::size_t{};
   while (padding_size < normalized.size() && normalized[normalized.size() - padding_size - 1U] == '=') {
      ++padding_size;
   }
   if (padding_size > 2U) {
      fail("base64 padding is invalid");
   }
   if (options.pad == padding_policy::forbid && padding_size != 0U) {
      fail("base64 padding is forbidden");
   }

   const auto data_size = normalized.size() - padding_size;
   const auto remainder = data_size % 4U;
   if (remainder == 1U) {
      fail("base64 length is invalid");
   }
   const auto expected_padding = remainder == 0U ? 0U : 4U - remainder;
   if (padding_size != 0U && (normalized.size() % 4U != 0U || padding_size != expected_padding)) {
      fail("base64 padding is invalid");
   }
   if (options.pad == padding_policy::require && padding_size != expected_padding) {
      fail("base64 padding is required");
   }

   for (auto index = std::size_t{}; index < normalized.size(); ++index) {
      if (normalized[index] == '=') {
         if (index < data_size) {
            fail("base64 padding is invalid");
         }
         continue;
      }
      if (decode_character(normalized[index], options.characters) < 0) {
         fail("encountered non-base64 character");
      }
   }

   if (remainder == 2U && (decode_character(normalized[data_size - 1U], options.characters) & 0x0f) != 0) {
      fail("base64 trailing bits are non-canonical");
   }
   if (remainder == 3U && (decode_character(normalized[data_size - 1U], options.characters) & 0x03) != 0) {
      fail("base64 trailing bits are non-canonical");
   }

   auto output = std::vector<std::uint8_t>{};
   output.reserve(data_size);
   auto accumulator = std::uint32_t{};
   auto available_bits = 0;
   for (auto index = std::size_t{}; index < data_size; ++index) {
      accumulator =
          (accumulator << 6U) | static_cast<std::uint32_t>(decode_character(normalized[index], options.characters));
      available_bits += 6;
      if (available_bits >= 8) {
         available_bits -= 8;
         output.push_back(static_cast<std::uint8_t>((accumulator >> available_bits) & 0xffU));
      }
   }
   return output;
}

} // namespace forge::codec::base64
