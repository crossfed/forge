module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <chrono>
#include <cstdint>
#include <new>
#include <string>
#include <string_view>
#include <vector>

export module forge.chain.types;

import forge.crypto.asymmetric;
import forge.crypto.ripemd160;
import forge.crypto.sha256;
import forge.crypto.sha512;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.described;

export namespace forge::chain {

using bytes = std::vector<char>;
using chain_id = forge::crypto::sha256;
using block_id = forge::crypto::sha256;
using checksum = forge::crypto::sha256;
using checksum256 = forge::crypto::sha256;
using checksum512 = forge::crypto::sha512;
using checksum160 = forge::crypto::ripemd160;
using transaction_id = checksum;
using digest = checksum;
using public_key = forge::crypto::asymmetric::public_key;
using signature = forge::crypto::asymmetric::signature;
using weight = std::uint16_t;
using block_num = std::uint32_t;
using share = std::int64_t;
using int128_t = __int128;
using uint128_t = unsigned __int128;
using extensions = std::vector<std::pair<std::uint16_t, bytes>>;

struct name {
   std::uint64_t value = 0;

   constexpr name(std::uint64_t raw = 0)
       : value(raw) {}

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

   constexpr explicit symbol_code(std::uint64_t raw = 0)
       : value(raw) {}

   constexpr std::uint64_t raw() const {
      return value;
   }

   constexpr bool operator==(const symbol_code&) const = default;
};

struct symbol {
   std::uint64_t value = 0;

   constexpr symbol(std::uint64_t raw = 0)
       : value(raw) {}

   constexpr symbol(symbol_code code, std::uint8_t precision)
       : value((code.raw() << 8U) | precision) {}

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

   constexpr asset(std::int64_t raw_amount = 0, symbol raw_symbol = {})
       : amount(raw_amount)
       , sym(raw_symbol) {}

   constexpr bool operator==(const asset&) const = default;
};

struct block_timestamp {
   std::uint32_t slot = 0;

   constexpr block_timestamp(std::uint32_t raw_slot = 0)
       : slot(raw_slot) {}

   constexpr auto operator<=>(const block_timestamp&) const = default;
};

[[noreturn]] void fail_invalid_argument(const char* message);

std::uint64_t encode_name(std::string_view value);
std::string decode_name(std::uint64_t raw);
name make_name(std::string_view value);
std::string to_string(const name& value);

std::uint64_t encode_symbol_code(std::string_view code);
std::string decode_symbol_code(std::uint64_t raw);
symbol_code make_symbol_code(std::string_view code);
symbol make_symbol(std::string_view code, std::uint8_t precision);
std::string to_string(const symbol_code& value);
std::string to_string(const symbol& value);

void to_variant(const name& value, forge::variant& variant);
void from_variant(const forge::variant& variant, name& value);
void to_variant(const symbol_code& value, forge::variant& variant);
void from_variant(const forge::variant& variant, symbol_code& value);
void to_variant(const symbol& value, forge::variant& variant);
void from_variant(const forge::variant& variant, symbol& value);
void to_variant(const asset& value, forge::variant& variant);
void from_variant(const forge::variant& variant, asset& value);

} // namespace forge::chain

export namespace forge::chain {
BOOST_DESCRIBE_STRUCT(name, (), (value))
BOOST_DESCRIBE_STRUCT(permission_level, (), (actor, permission))
BOOST_DESCRIBE_STRUCT(symbol_code, (), (value))
BOOST_DESCRIBE_STRUCT(symbol, (), (value))
BOOST_DESCRIBE_STRUCT(asset, (), (amount, sym))
BOOST_DESCRIBE_STRUCT(block_timestamp, (), (slot))
}

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::name)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::permission_level)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::symbol_code)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::symbol)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::asset)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::block_timestamp)
