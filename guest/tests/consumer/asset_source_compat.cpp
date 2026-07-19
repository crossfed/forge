#include <eosio/asset.hpp>

import forge.chain.protocol.values;

namespace {

constexpr auto modern_asset = forge::chain::protocol::asset{42};
constexpr auto legacy_asset = eosio::asset{42};

static_assert(modern_asset.amount == 42);
static_assert(modern_asset.sym.raw() == 0U);
static_assert(legacy_asset.amount == 42);
static_assert(legacy_asset.sym.raw() == 0U);

} // namespace
