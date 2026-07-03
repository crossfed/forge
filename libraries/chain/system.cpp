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
namespace {

account_name system_account() {
   return make_name("eosio");
}

} // namespace

account_name newaccount::get_account() { return system_account(); }
action_name newaccount::get_name() { return make_name("newaccount"); }
account_name setcode::get_account() { return system_account(); }
action_name setcode::get_name() { return make_name("setcode"); }
account_name setabi::get_account() { return system_account(); }
action_name setabi::get_name() { return make_name("setabi"); }
account_name updateauth::get_account() { return system_account(); }
action_name updateauth::get_name() { return make_name("updateauth"); }
account_name deleteauth::get_account() { return system_account(); }
action_name deleteauth::get_name() { return make_name("deleteauth"); }
account_name linkauth::get_account() { return system_account(); }
action_name linkauth::get_name() { return make_name("linkauth"); }
account_name unlinkauth::get_account() { return system_account(); }
action_name unlinkauth::get_name() { return make_name("unlinkauth"); }
account_name canceldelay::get_account() { return system_account(); }
action_name canceldelay::get_name() { return make_name("canceldelay"); }
account_name onerror::get_account() { return system_account(); }
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
