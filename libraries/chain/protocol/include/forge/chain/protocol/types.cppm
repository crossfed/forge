module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <vector>

export module forge.chain.protocol.types;

export import forge.chain.core.types;
export import forge.chain.protocol.values;

import forge.crypto.asymmetric;
import forge.crypto.ripemd160;
import forge.crypto.sha256;
import forge.crypto.sha512;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.described;
import forge.variant.value;

export namespace forge::chain::protocol {

using bytes = std::vector<std::uint8_t>;
using digest = forge::chain::core::digest;
using chain_id = digest;
using block_id = digest;
using checksum = digest;
using checksum256 = digest;
using checksum512 = forge::crypto::sha512;
using checksum160 = forge::crypto::ripemd160;
using transaction_id = checksum;
using public_key = forge::crypto::asymmetric::public_key;
using signature = forge::crypto::asymmetric::signature;
using weight = std::uint16_t;
using block_num = std::uint32_t;
using share = std::int64_t;
using int128_t = __int128;
using uint128_t = unsigned __int128;
using extensions = std::vector<std::pair<std::uint16_t, bytes>>;

void to_variant(const name& value, forge::variant& variant);
void from_variant(const forge::variant& variant, name& value);
void to_variant(const symbol_code& value, forge::variant& variant);
void from_variant(const forge::variant& variant, symbol_code& value);
void to_variant(const symbol& value, forge::variant& variant);
void from_variant(const forge::variant& variant, symbol& value);
void to_variant(const asset& value, forge::variant& variant);
void from_variant(const forge::variant& variant, asset& value);

} // namespace forge::chain::protocol

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(name, (), (value))
BOOST_DESCRIBE_STRUCT(permission_level, (), (actor, permission))
BOOST_DESCRIBE_STRUCT(symbol_code, (), (value))
BOOST_DESCRIBE_STRUCT(symbol, (), (value))
BOOST_DESCRIBE_STRUCT(asset, (), (amount, sym))
BOOST_DESCRIBE_STRUCT(block_timestamp, (), (slot))
} // namespace forge::chain::protocol
