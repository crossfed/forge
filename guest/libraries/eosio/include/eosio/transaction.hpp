#pragma once

#include <eosio/action.hpp>

import forge.contract.transaction;

namespace eosio {

using forge::contract::expiration;
using forge::contract::get_action;
using forge::contract::get_context_free_data;
using forge::contract::get_transaction;
using forge::contract::read_transaction;
using forge::contract::tapos_block_num;
using forge::contract::tapos_block_prefix;
using forge::contract::transaction_size;
using forge::chain::protocol::transaction;
using forge::chain::protocol::transaction_header;

} // namespace eosio
