#pragma once

#include <cstddef>

import forge.chain.protocol.fixed_key;

namespace eosio {

template <std::size_t Size> using fixed_bytes = forge::chain::protocol::fixed_key<Size>;
using checksum256 = fixed_bytes<32>;

} // namespace eosio
