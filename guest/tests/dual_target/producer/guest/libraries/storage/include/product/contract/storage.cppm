module;

#include <cstdint>
#include <optional>

export module product.contract.storage;

import product.chain.protocol;

export namespace product::contract::storage {

[[nodiscard]] std::optional<std::uint64_t>
checked_size(const chain::begin_revision& request);

} // namespace product::contract::storage
