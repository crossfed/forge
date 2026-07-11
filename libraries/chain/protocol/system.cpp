module;

#include <forge/raw/serialization.hpp>

#include <new>

module forge.chain.protocol.system;

import forge.crypto.sha256;
import forge.raw.datastream;
import forge.raw.raw;
import forge.variant.value;
import forge.variant.conversion;
import forge.variant.containers;
import forge.variant.chrono;
import forge.variant.multiprecision;
import forge.variant.format;
import forge.variant.described;

namespace forge::chain::protocol {

action_name newaccount::get_name() { return make_name("newaccount"); }
action_name setcode::get_name() { return make_name("setcode"); }
action_name setabi::get_name() { return make_name("setabi"); }
action_name updateauth::get_name() { return make_name("updateauth"); }
action_name deleteauth::get_name() { return make_name("deleteauth"); }
action_name linkauth::get_name() { return make_name("linkauth"); }
action_name unlinkauth::get_name() { return make_name("unlinkauth"); }
action_name canceldelay::get_name() { return make_name("canceldelay"); }
action_name onerror::get_name() { return make_name("onerror"); }

} // namespace forge::chain::protocol

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::newaccount)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::setcode)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::setabi)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::updateauth)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::deleteauth)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::linkauth)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::unlinkauth)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::canceldelay)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::protocol::onerror)
