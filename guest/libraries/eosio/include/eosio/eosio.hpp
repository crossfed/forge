#pragma once

#include <eosio/action.hpp>
#include <eosio/asset.hpp>
#include <eosio/base64.hpp>
#include <eosio/binary_extension.hpp>
#include <eosio/bitset.hpp>
#include <eosio/call.hpp>
#include <eosio/check.hpp>
#include <eosio/contract.hpp>
#include <eosio/context.hpp>
#include <eosio/crypto.hpp>
#include <eosio/crypto_bls_ext.hpp>
#include <eosio/crypto_ext.hpp>
#include <eosio/datastream.hpp>
#include <eosio/deferred_transaction.hpp>
#include <eosio/dispatcher.hpp>
#include <eosio/fixed_bytes.hpp>
#include <eosio/ignore.hpp>
#include <eosio/instant_finality.hpp>
#include <eosio/key_utils.hpp>
#include <eosio/multi_index.hpp>
#include <eosio/name.hpp>
#include <eosio/permission.hpp>
#include <eosio/powers.hpp>
#include <eosio/print.hpp>
#include <eosio/privileged.hpp>
#include <eosio/producer_schedule.hpp>
#include <eosio/reflect.hpp>
#include <eosio/rope.hpp>
#include <eosio/serialize.hpp>
#include <eosio/singleton.hpp>
#include <eosio/string.hpp>
#include <eosio/symbol.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>
#include <eosio/transaction.hpp>
#include <eosio/varint.hpp>

#include <cstdlib>

import forge.chain.protocol.values;
import forge.contract.dispatcher;
import forge.contract.intrinsics;
import forge.raw.codec;

namespace eosio {

using forge::chain::protocol::permission_level;
using forge::chain::protocol::symbol;
using forge::chain::protocol::symbol_code;
using forge::chain::protocol::uint128_t;
using forge::contract::check;
using forge::contract::current_receiver;

} // namespace eosio
