#pragma once

import forge.contract.singleton;

namespace eosio {

template <forge::chain::protocol::name::raw SingletonName, class T>
using singleton = forge::contract::singleton<SingletonName, T>;

} // namespace eosio
