module;

#include <cstdint>
#include <optional>

export module product.contract.storage;

import product.chain.protocol;

export namespace product::contract::storage {

std::optional<std::uint64_t> checked_size(chain::begin_revision const& request);

}
