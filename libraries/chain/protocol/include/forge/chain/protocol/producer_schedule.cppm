module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>
#endif

export module forge.chain.protocol.producer_schedule;

export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(producer_key, (), (producer_name, block_signing_key))
BOOST_DESCRIBE_STRUCT(producer_schedule, (), (version, producers))
} // namespace forge::chain::protocol

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::producer_key)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::producer_schedule)
#endif
