#pragma once

#include <eosio/name.hpp>
#include <eosio/symbol.hpp>

import forge.contract.compatibility.asset;

namespace eosio {

using asset = forge::contract::compatibility::asset;
using extended_asset = forge::contract::compatibility::extended_asset;

} // namespace eosio
