module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>
#endif

export module forge.chain.protocol.finalizer_authority;

export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.described;
import forge.variant.value;

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(finalizer_authority, (), (description, weight, public_key))
} // namespace forge::chain::protocol

FORGE_DECLARE_SERIALIZATION(forge::chain::protocol::finalizer_authority)
#endif
