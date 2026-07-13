module;

#include <boost/describe.hpp>
#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <span>
#include <vector>

export module forge.chain.protocol.action;

export import forge.chain.protocol.types;
import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.described;

export namespace forge::chain::protocol {

struct action_base {
   account_name account;
   action_name name;
   std::vector<permission_level> authorization;
};

struct action : action_base {
   bytes data;
};

core::digest generate_action_digest(const action& value, std::span<const std::uint8_t> return_value);

} // namespace forge::chain::protocol

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(action_base, (), (account, name, authorization))
BOOST_DESCRIBE_STRUCT(action, (action_base), (data))
} // namespace forge::chain::protocol

FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::action_base)
FORGE_DECLARE_SERIALIZATION_PACK(forge::chain::protocol::action)
