module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>
#endif

export module forge.chain.protocol.authority;

export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(permission_level_weight, (), (permission, weight))
BOOST_DESCRIBE_STRUCT(key_weight, (), (key, weight))
BOOST_DESCRIBE_STRUCT(wait_weight, (), (wait_sec, weight))
BOOST_DESCRIBE_STRUCT(authority, (), (threshold, keys, accounts, waits))
} // namespace forge::chain::protocol

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::permission_level_weight)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::key_weight)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::wait_weight)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::authority)
#endif
