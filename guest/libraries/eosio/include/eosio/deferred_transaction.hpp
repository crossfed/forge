#pragma once

#include <eosio/transaction.hpp>

import forge.contract.deferred_transaction;

namespace eosio {

using forge::contract::cancel_deferred;
using forge::contract::deferred_transaction;
using forge::contract::onerror;
using forge::contract::send_deferred;

} // namespace eosio
