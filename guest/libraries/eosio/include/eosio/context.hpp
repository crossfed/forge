#pragma once

#include <eosio/name.hpp>

import forge.contract.action;

namespace eosio {

inline name current_context_contract() {
   return forge::contract::current_receiver();
}

} // namespace eosio
