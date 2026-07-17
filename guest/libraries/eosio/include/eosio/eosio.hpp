#pragma once

#include <eosio/contract.hpp>
#include <eosio/dispatcher.hpp>
#include <eosio/fixed_bytes.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/serialize.hpp>
#include <eosio/singleton.hpp>

import forge.chain.protocol.values;
import forge.contract.dispatcher;
import forge.contract.intrinsics;
import forge.raw.codec;

namespace eosio {

using forge::chain::protocol::action_name;
using forge::chain::protocol::asset;
using forge::chain::protocol::name;
using forge::chain::protocol::permission_level;
using forge::chain::protocol::symbol;
using forge::chain::protocol::symbol_code;
using forge::chain::protocol::uint128_t;
using forge::contract::action_data_size;
using forge::contract::check;
using forge::contract::current_receiver;
using forge::contract::read_action_data;

} // namespace eosio

using forge::chain::protocol::literals::operator""_n;
