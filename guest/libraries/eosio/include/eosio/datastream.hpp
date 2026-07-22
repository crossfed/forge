#pragma once

#include <array>
#include <list>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include <string.h>

import forge.contract.datastream;

namespace eosio {

template <typename Storage> using datastream = ::forge::datastream<Storage>;

} // namespace eosio
