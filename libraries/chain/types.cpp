module;

#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

module forge.chain.types;

import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.format;
import forge.variant.described;

namespace forge::chain {
namespace {

constexpr auto charmap = std::string_view{".12345abcdefghijklmnopqrstuvwxyz"};
constexpr auto max_name_last_symbol = std::uint8_t{0x0f};

std::uint8_t symbol_index(char value) {
   const auto found = charmap.find(value);
   if (found == std::string_view::npos) {
      fail_invalid_argument("invalid chain name character");
   }
   return static_cast<std::uint8_t>(found);
}

bool is_digit(char value) {
   return value >= '0' && value <= '9';
}

std::uint64_t parse_decimal_digits(std::string_view text, std::uint64_t limit, const char* invalid_message) {
   if (text.empty()) {
      fail_invalid_argument(invalid_message);
   }

   std::uint64_t result = 0;
   for (const auto value : text) {
      if (!is_digit(value)) {
         fail_invalid_argument(invalid_message);
      }

      const auto digit = static_cast<std::uint64_t>(value - '0');
      if (result > limit / 10U || (result == limit / 10U && digit > limit % 10U)) {
         fail_invalid_argument("chain numeric value is out of range");
      }
      result = result * 10U + digit;
   }
   return result;
}

symbol parse_symbol(std::string_view text) {
   const auto comma = text.find(',');
   if (comma == std::string_view::npos || comma == 0 || comma + 1U == text.size()
       || text.find(',', comma + 1U) != std::string_view::npos) {
      fail_invalid_argument("chain symbol text must be '<precision>,<code>'");
   }

   const auto precision = parse_decimal_digits(
      text.substr(0, comma),
      std::numeric_limits<std::uint8_t>::max(),
      "chain symbol precision must use decimal digits"
   );
   return make_symbol(text.substr(comma + 1U), static_cast<std::uint8_t>(precision));
}

asset parse_asset(std::string_view text) {
   const auto space = text.find(' ');
   if (space == std::string_view::npos || space == 0 || space + 1U == text.size()
       || text.find(' ', space + 1U) != std::string_view::npos) {
      fail_invalid_argument("chain asset text must be '<amount> <symbol>'");
   }

   auto amount_text = text.substr(0, space);
   const auto symbol_text = text.substr(space + 1U);

   auto negative = false;
   if (amount_text.front() == '+') {
      fail_invalid_argument("chain asset amount must not use explicit plus sign");
   }
   if (amount_text.front() == '-') {
      negative = true;
      amount_text.remove_prefix(1);
      if (amount_text.empty()) {
         fail_invalid_argument("chain asset amount is empty");
      }
   }

   const auto dot = amount_text.find('.');
   auto whole = amount_text;
   auto fraction = std::string_view{};
   if (dot != std::string_view::npos) {
      if (amount_text.find('.', dot + 1U) != std::string_view::npos) {
         fail_invalid_argument("chain asset amount has multiple decimal points");
      }
      whole = amount_text.substr(0, dot);
      fraction = amount_text.substr(dot + 1U);
      if (whole.empty() || fraction.empty()) {
         fail_invalid_argument("chain asset amount must have whole and fractional digits");
      }
      if (fraction.size() > std::numeric_limits<std::uint8_t>::max()) {
         fail_invalid_argument("chain asset precision is out of range");
      }
   }

   auto digits = std::string{};
   digits.reserve(whole.size() + fraction.size());
   digits += whole;
   digits += fraction;

   const auto positive_limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
   const auto negative_limit = positive_limit + 1U;
   const auto magnitude = parse_decimal_digits(
      digits,
      negative ? negative_limit : positive_limit,
      "chain asset amount must use decimal digits"
   );

   auto amount = std::int64_t{0};
   if (negative) {
      amount = magnitude == negative_limit
         ? std::numeric_limits<std::int64_t>::min()
         : -static_cast<std::int64_t>(magnitude);
   } else {
      amount = static_cast<std::int64_t>(magnitude);
   }

   return asset{amount, make_symbol(symbol_text, static_cast<std::uint8_t>(fraction.size()))};
}

std::string format_asset_amount(std::int64_t amount, std::uint8_t precision) {
   const auto negative = amount < 0;
   const auto magnitude = negative
      ? std::uint64_t{0} - static_cast<std::uint64_t>(amount)
      : static_cast<std::uint64_t>(amount);

   auto digits = std::to_string(magnitude);
   auto result = std::string{};
   if (negative) {
      result.push_back('-');
   }

   if (precision == 0) {
      result += digits;
      return result;
   }

   if (digits.size() <= precision) {
      result += "0.";
      result.append(static_cast<std::size_t>(precision) - digits.size(), '0');
      result += digits;
      return result;
   }

   const auto decimal_position = digits.size() - precision;
   result += digits.substr(0, decimal_position);
   result.push_back('.');
   result += digits.substr(decimal_position);
   return result;
}

} // namespace

[[noreturn]] void fail_invalid_argument(const char* message) {
   throw std::invalid_argument(message);
}

std::uint64_t encode_name(std::string_view value) {
   if (value.size() > 13) {
      fail_invalid_argument("chain name is longer than 13 characters");
   }

   std::uint64_t result = 0;
   for (std::size_t i = 0; i < 13; ++i) {
      const auto c = i < value.size() ? symbol_index(value[i]) : std::uint8_t{0};
      if (i < 12) {
         result |= (static_cast<std::uint64_t>(c) & 0x1fULL) << (64U - 5U * (i + 1U));
      } else {
         if (c > max_name_last_symbol) {
            fail_invalid_argument("chain name 13th character is outside the allowed range");
         }
         result |= static_cast<std::uint64_t>(c) & 0x0fULL;
      }
   }
   return result;
}

std::string decode_name(std::uint64_t raw) {
   auto result = std::string(13, '.');
   auto tmp = raw;
   for (std::uint32_t i = 0; i <= 12; ++i) {
      const auto c = charmap[tmp & (i == 0 ? 0x0f : 0x1f)];
      result[12 - i] = c;
      tmp >>= (i == 0 ? 4 : 5);
   }
   const auto last = result.find_last_not_of('.');
   if (last == std::string::npos) {
      return {};
   }
   result.resize(last + 1);
   return result;
}

name make_name(std::string_view value) {
   return name{encode_name(value)};
}

std::string to_string(const name& value) {
   return decode_name(value.value);
}

std::uint64_t encode_symbol_code(std::string_view code) {
   if (code.empty() || code.size() > 7) {
      fail_invalid_argument("chain symbol code size is invalid");
   }
   std::uint64_t result = 0;
   for (std::size_t i = 0; i < code.size(); ++i) {
      if (code[i] < 'A' || code[i] > 'Z') {
         fail_invalid_argument("chain symbol code must use A-Z");
      }
      result |= static_cast<std::uint64_t>(code[i]) << (8U * i);
   }
   return result;
}

std::string decode_symbol_code(std::uint64_t raw) {
   std::string result;
   for (std::size_t i = 0; i < 7; ++i) {
      const auto c = static_cast<char>((raw >> (8U * i)) & 0xffU);
      if (c == 0) {
         break;
      }
      result.push_back(c);
   }
   return result;
}

symbol_code make_symbol_code(std::string_view code) {
   return symbol_code{encode_symbol_code(code)};
}

symbol make_symbol(std::string_view code, std::uint8_t precision) {
   return symbol{make_symbol_code(code), precision};
}

std::string to_string(const symbol_code& value) {
   return decode_symbol_code(value.value);
}

std::string to_string(const symbol& value) {
   return std::to_string(value.precision()) + "," + to_string(value.code());
}

void to_variant(const name& value, forge::variant& variant) {
   variant = to_string(value);
}

void from_variant(const forge::variant& variant, name& value) {
   value = make_name(variant.as_string());
}

void to_variant(const symbol_code& value, forge::variant& variant) {
   variant = to_string(value);
}

void from_variant(const forge::variant& variant, symbol_code& value) {
   value = make_symbol_code(variant.as_string());
}

void to_variant(const symbol& value, forge::variant& variant) {
   variant = to_string(value);
}

void from_variant(const forge::variant& variant, symbol& value) {
   value = parse_symbol(variant.as_string());
}

void to_variant(const asset& value, forge::variant& variant) {
   variant = format_asset_amount(value.amount, value.sym.precision()) + " " + to_string(value.sym.code());
}

void from_variant(const forge::variant& variant, asset& value) {
   value = parse_asset(variant.as_string());
}

} // namespace forge::chain

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::name)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::permission_level)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::symbol_code)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::symbol)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::asset)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::block_timestamp)
