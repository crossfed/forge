#pragma once

#include <eosio/fixed_bytes.hpp>
#include <eosio/name.hpp>
#include <eosio/time.hpp>

import forge.contract.system;

namespace eosio {

using forge::contract::block_num_t;
using forge::contract::current_block_number;
using forge::contract::current_block_time;
using forge::contract::current_time_point;
using forge::contract::eosio_exit;
using forge::contract::get_sender;
using forge::contract::is_feature_activated;

} // namespace eosio
