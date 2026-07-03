module;

#include <forge/raw/serialization.hpp>

#include <cstdint>
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

std::uint8_t symbol_index(char value) {
   const auto found = charmap.find(value);
   if (found == std::string_view::npos) {
      fail_invalid_argument("invalid chain name character");
   }
   return static_cast<std::uint8_t>(found);
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

void from_variant(const forge::variant&, symbol&) {
   fail_invalid_argument("chain symbol variant parsing is not part of this block");
}

void to_variant(const asset& value, forge::variant& variant) {
   variant = format_asset_amount(value.amount, value.sym.precision()) + " " + to_string(value.sym.code());
}

void from_variant(const forge::variant&, asset&) {
   fail_invalid_argument("chain asset variant parsing is not part of this block");
}

} // namespace forge::chain

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::name)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::permission_level)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::symbol_code)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::symbol)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::asset)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::block_timestamp)
