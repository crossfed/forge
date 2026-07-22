module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

export module forge.chain.protocol.types;

export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
export import forge.chain.core.types;

import forge.variant.value;

export namespace forge::chain::protocol {

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
BOOST_DESCRIBE_STRUCT(extended_symbol, (), (symbol, contract))
BOOST_DESCRIBE_STRUCT(extended_asset, (), (quantity, contract))
BOOST_DESCRIBE_STRUCT(block_timestamp, (), (slot))
} // namespace forge::chain::protocol
#endif
