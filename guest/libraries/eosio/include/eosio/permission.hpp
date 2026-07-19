#pragma once

#include <eosio/action.hpp>
#include <eosio/time.hpp>

import forge.contract.authorization;

namespace eosio {

using forge::contract::check_permission_authorization;
using forge::contract::check_transaction_authorization;
using forge::contract::get_account_creation_time;
using forge::contract::get_permission_last_used;

} // namespace eosio
