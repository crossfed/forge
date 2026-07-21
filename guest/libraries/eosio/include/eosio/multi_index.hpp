#pragma once

#include <eosio/fixed_bytes.hpp>
#include <eosio/internal/db.hpp>

import forge.contract.multi_index;
import forge.chain.protocol.values;

namespace eosio {

using forge::contract::const_mem_fun;
using forge::contract::indexed_by;
using forge::contract::same_payer;

template <forge::chain::protocol::name::raw TableName, class T, class... Indices>
using multi_index = forge::contract::multi_index<TableName, T, Indices...>;

} // namespace eosio
