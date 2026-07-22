module;

#include <forge/raw/serialization.hpp>

#include <cstdint>
#include <span>

module forge.chain.protocol.action;

import forge.crypto.digest.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.described;

namespace forge::chain::protocol {

core::digest generate_action_digest(const action& value, std::span<const std::uint8_t> return_value) {
   const auto output = bytes{return_value.begin(), return_value.end()};
   const auto base_digest = core::digest::packhash(static_cast<const action_base&>(value));
   const auto data_digest = core::digest::packhash(value.data, output);
   return core::digest::packhash(base_digest, data_digest);
}

} // namespace forge::chain::protocol

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::action_base)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::action)
