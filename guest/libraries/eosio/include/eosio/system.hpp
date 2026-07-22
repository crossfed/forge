#pragma once

#include <eosio/check.hpp>
#include <eosio/fixed_bytes.hpp>
#include <eosio/internal/system.hpp>
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

[[nodiscard]] inline bool is_feature_activated(const checksum256& digest) {
   return forge::contract::is_feature_activated(detail::to_digest<forge::contract::checksum256>(digest));
}

} // namespace eosio
