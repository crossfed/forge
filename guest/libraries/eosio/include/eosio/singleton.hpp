#pragma once

#include <eosio/multi_index.hpp>
#include <eosio/system.hpp>

import forge.contract.singleton;
import forge.chain.protocol.values;

namespace eosio {

template <forge::chain::protocol::name::raw SingletonName, class T>
using singleton = forge::contract::singleton<SingletonName, T>;

} // namespace eosio
