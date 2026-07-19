#pragma once

#include <eosio/name.hpp>

import forge.contract.producer_schedule;

namespace eosio {

using forge::chain::protocol::block_signing_authority;
using forge::chain::protocol::block_signing_authority_v0;
using forge::chain::protocol::key_weight;
using forge::chain::protocol::producer_authority;
using forge::chain::protocol::producer_authority_schedule;
using forge::chain::protocol::producer_key;
using forge::chain::protocol::producer_schedule;
using forge::contract::get_active_producers;

} // namespace eosio
