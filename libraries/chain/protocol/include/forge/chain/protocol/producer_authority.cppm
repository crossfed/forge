module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>
#endif

export module forge.chain.protocol.producer_authority;

export import forge.chain.protocol.authority;
export import forge.chain.protocol.producer_schedule;
export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.described;
import forge.variant.value;

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(block_signing_authority_v0, (), (threshold, keys))
BOOST_DESCRIBE_STRUCT(producer_authority, (), (producer_name, authority))
BOOST_DESCRIBE_STRUCT(producer_authority_schedule, (), (version, producers))
} // namespace forge::chain::protocol

FORGE_DECLARE_SERIALIZATION(forge::chain::protocol::block_signing_authority_v0)
FORGE_DECLARE_SERIALIZATION(forge::chain::protocol::producer_authority)
FORGE_DECLARE_SERIALIZATION(forge::chain::protocol::producer_authority_schedule)
#endif
