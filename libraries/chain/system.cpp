module;

#include <forge/raw/serialization.hpp>

#include <new>

module forge.chain.system;

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

namespace forge::chain {

action_name newaccount::get_name() { return make_name("newaccount"); }
action_name setcode::get_name() { return make_name("setcode"); }
action_name setabi::get_name() { return make_name("setabi"); }
action_name updateauth::get_name() { return make_name("updateauth"); }
action_name deleteauth::get_name() { return make_name("deleteauth"); }
action_name linkauth::get_name() { return make_name("linkauth"); }
action_name unlinkauth::get_name() { return make_name("unlinkauth"); }
action_name canceldelay::get_name() { return make_name("canceldelay"); }
action_name onerror::get_name() { return make_name("onerror"); }

} // namespace forge::chain

FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::newaccount)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::setcode)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::setabi)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::updateauth)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::deleteauth)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::linkauth)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::unlinkauth)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::canceldelay)
FORGE_IMPLEMENT_SERIALIZATION_PACK(forge::chain::onerror)
