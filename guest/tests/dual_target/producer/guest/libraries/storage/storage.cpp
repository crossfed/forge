module;

#include <cstdint>
#include <optional>

module product.contract.storage;

namespace product::contract::storage {

std::optional<std::uint64_t>
checked_size(const chain::begin_revision& request) {
   return chain::checked_add(request.size, 0);
}

} // namespace product::contract::storage
