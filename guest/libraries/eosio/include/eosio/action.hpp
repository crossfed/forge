#pragma once

#include <eosio/internal/action.hpp>
#include <eosio/asset.hpp>
#include <eosio/datastream.hpp>
#include <eosio/fixed_bytes.hpp>
#include <eosio/name.hpp>
#include <eosio/serialize.hpp>
#include <eosio/time.hpp>
#include <eosio/varint.hpp>

import forge.contract.action;
import forge.contract.intrinsics;

using uint128_t = unsigned __int128;
using int128_t = __int128;

namespace eosio {

using forge::chain::protocol::permission_level;
using forge::contract::action;
using forge::contract::action_data_size;
using forge::contract::action_wrapper;
using forge::contract::code_hash_result;
using forge::contract::current_receiver;
using forge::contract::get_code_hash;
using forge::contract::has_auth;
using forge::contract::inline_dispatcher;
using forge::contract::is_account;
using forge::contract::publication_time;
using forge::contract::read_action_data;
using forge::contract::require_auth;
using forge::contract::require_recipient;
using forge::contract::unpack_action_data;
using forge::contract::variant_action_wrapper;

} // namespace eosio

#define INLINE_ACTION_SENDER3(contract_type, function_name, action_name)                                               \
   ::eosio::inline_dispatcher<decltype(&contract_type::function_name), action_name>::call
#define INLINE_ACTION_SENDER2(contract_type, function_name)                                                            \
   INLINE_ACTION_SENDER3(contract_type, function_name, ::eosio::name{#function_name})
#define FORGE_EOSIO_INLINE_SELECT(_1, _2, _3, selected, ...) selected
#define INLINE_ACTION_SENDER(...)                                                                                      \
   FORGE_EOSIO_INLINE_SELECT(__VA_ARGS__, INLINE_ACTION_SENDER3, INLINE_ACTION_SENDER2)(__VA_ARGS__)
#define SEND_INLINE_ACTION(contract, function_name, ...)                                                               \
   INLINE_ACTION_SENDER(std::decay_t<decltype(contract)>, function_name)((contract).get_self(), __VA_ARGS__)
