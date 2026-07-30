module;

#include <cstdint>
#include <optional>

module product.contract.storage;

namespace product::contract::storage {

std::optional<std::uint64_t> checked_size(chain::begin_revision const& request) {
   return chain::checked_add(request.size, 0);
}

}
