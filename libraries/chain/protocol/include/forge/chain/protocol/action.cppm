module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <span>
#endif

export module forge.chain.protocol.action;

export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;

export namespace forge::chain::protocol {

digest generate_action_digest(const action& value, std::span<const std::uint8_t> return_value);

} // namespace forge::chain::protocol

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(action_base, (), (account, name, authorization))
BOOST_DESCRIBE_STRUCT(action, (action_base), (data))
} // namespace forge::chain::protocol

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::action_base)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::action)
#endif
