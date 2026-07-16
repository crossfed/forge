module;

#include <cstdint>
#include <string>
#include <string_view>

export module forge.chain.protocol.values;

import forge.raw.codec;

export namespace forge::chain::protocol {

namespace detail {
[[noreturn]] void fail_value(const char* message);
}

struct name {
   std::uint64_t value = 0;

   constexpr name(std::uint64_t raw = 0) : value(raw) {}

   constexpr bool operator==(const name&) const = default;
   constexpr auto operator<=>(const name&) const = default;
};

using account_name = name;
using action_name = name;
using permission_name = name;
using table_name = name;

struct permission_level {
   account_name actor;
   permission_name permission;

   constexpr bool operator==(const permission_level&) const = default;
   constexpr auto operator<=>(const permission_level&) const = default;
};

struct symbol_code {
   std::uint64_t value = 0;

   constexpr explicit symbol_code(std::uint64_t raw = 0) : value(raw) {}

   constexpr std::uint64_t raw() const {
      return value;
   }

   constexpr bool operator==(const symbol_code&) const = default;
};

struct symbol {
   std::uint64_t value = 0;

   constexpr symbol(std::uint64_t raw = 0) : value(raw) {}

   constexpr symbol(symbol_code code, std::uint8_t precision) : value((code.raw() << 8U) | precision) {}

   constexpr std::uint64_t raw() const {
      return value;
   }

   constexpr std::uint8_t precision() const {
      return static_cast<std::uint8_t>(value & 0xffU);
   }

   constexpr symbol_code code() const {
      return symbol_code{value >> 8U};
   }

   constexpr bool operator==(const symbol&) const = default;
};

struct asset {
   std::int64_t amount = 0;
   symbol sym{};

   constexpr asset(std::int64_t raw_amount = 0, symbol raw_symbol = {}) : amount(raw_amount), sym(raw_symbol) {}

   constexpr bool operator==(const asset&) const = default;
};

struct block_timestamp {
   std::uint32_t slot = 0;

   constexpr block_timestamp(std::uint32_t raw_slot = 0) : slot(raw_slot) {}

   constexpr auto operator<=>(const block_timestamp&) const = default;
};

[[noreturn]] inline void fail_invalid_argument(const char* message) {
   detail::fail_value(message);
}

constexpr std::uint64_t encode_name(std::string_view text) {
   constexpr auto alphabet = std::string_view{".12345abcdefghijklmnopqrstuvwxyz"};
   if (text.size() > 13U) {
      fail_invalid_argument("chain name is longer than 13 characters");
   }

   auto result = std::uint64_t{0};
   for (auto index = std::size_t{0}; index < 13U; ++index) {
      auto encoded = std::uint8_t{0};
      if (index < text.size()) {
         const auto found = alphabet.find(text[index]);
         if (found == std::string_view::npos) {
            fail_invalid_argument("invalid chain name character");
         }
         encoded = static_cast<std::uint8_t>(found);
      }

      if (index < 12U) {
         result |= (static_cast<std::uint64_t>(encoded) & 0x1fULL) << (64U - 5U * (index + 1U));
      } else {
         if (encoded > 0x0fU) {
            fail_invalid_argument("chain name 13th character is outside the allowed range");
         }
         result |= static_cast<std::uint64_t>(encoded);
      }
   }
   return result;
}

inline std::string decode_name(std::uint64_t raw) {
   constexpr auto alphabet = std::string_view{".12345abcdefghijklmnopqrstuvwxyz"};
   auto result = std::string(13U, '.');
   for (auto index = std::uint32_t{0}; index <= 12U; ++index) {
      result[12U - index] = alphabet[raw & (index == 0U ? 0x0fU : 0x1fU)];
      raw >>= index == 0U ? 4U : 5U;
   }
   const auto last = result.find_last_not_of('.');
   if (last == std::string::npos) {
      return {};
   }
   result.resize(last + 1U);
   return result;
}

constexpr name make_name(std::string_view value) {
   return name{encode_name(value)};
}

inline std::string to_string(const name& value) {
   return decode_name(value.value);
}

inline std::uint64_t encode_symbol_code(std::string_view code) {
   if (code.empty() || code.size() > 7U) {
      fail_invalid_argument("chain symbol code size is invalid");
   }
   auto result = std::uint64_t{0};
   for (auto index = std::size_t{0}; index < code.size(); ++index) {
      if (code[index] < 'A' || code[index] > 'Z') {
         fail_invalid_argument("chain symbol code must use A-Z");
      }
      result |= static_cast<std::uint64_t>(code[index]) << (8U * index);
   }
   return result;
}

inline std::string decode_symbol_code(std::uint64_t raw) {
   auto result = std::string{};
   for (auto index = std::size_t{0}; index < 7U; ++index) {
      const auto value = static_cast<char>((raw >> (8U * index)) & 0xffU);
      if (value == 0) {
         break;
      }
      result.push_back(value);
   }
   return result;
}

inline symbol_code make_symbol_code(std::string_view code) {
   return symbol_code{encode_symbol_code(code)};
}

inline symbol make_symbol(std::string_view code, std::uint8_t precision) {
   return symbol{make_symbol_code(code), precision};
}

inline std::string to_string(const symbol_code& value) {
   return decode_symbol_code(value.value);
}

inline std::string to_string(const symbol& value) {
   return std::to_string(value.precision()) + "," + to_string(value.code());
}

template <typename Stream> void raw_pack(Stream& stream, const name& value) {
   forge::raw::pack(stream, value.value);
}

template <typename Stream> void raw_unpack(Stream& stream, name& value) {
   forge::raw::unpack(stream, value.value);
}

template <typename Stream> void raw_pack(Stream& stream, const permission_level& value) {
   forge::raw::pack(stream, value.actor);
   forge::raw::pack(stream, value.permission);
}

template <typename Stream> void raw_unpack(Stream& stream, permission_level& value) {
   forge::raw::unpack(stream, value.actor);
   forge::raw::unpack(stream, value.permission);
}

template <typename Stream> void raw_pack(Stream& stream, const symbol_code& value) {
   forge::raw::pack(stream, value.value);
}

template <typename Stream> void raw_unpack(Stream& stream, symbol_code& value) {
   forge::raw::unpack(stream, value.value);
}

template <typename Stream> void raw_pack(Stream& stream, const symbol& value) {
   forge::raw::pack(stream, value.value);
}

template <typename Stream> void raw_unpack(Stream& stream, symbol& value) {
   forge::raw::unpack(stream, value.value);
}

template <typename Stream> void raw_pack(Stream& stream, const asset& value) {
   forge::raw::pack(stream, value.amount);
   forge::raw::pack(stream, value.sym);
}

template <typename Stream> void raw_unpack(Stream& stream, asset& value) {
   forge::raw::unpack(stream, value.amount);
   forge::raw::unpack(stream, value.sym);
}

template <typename Stream> void raw_pack(Stream& stream, const block_timestamp& value) {
   forge::raw::pack(stream, value.slot);
}

template <typename Stream> void raw_unpack(Stream& stream, block_timestamp& value) {
   forge::raw::unpack(stream, value.slot);
}

} // namespace forge::chain::protocol
