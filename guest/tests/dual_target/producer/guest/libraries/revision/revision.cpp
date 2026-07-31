module;

#include <cstdint>

module product.contract.revision;

import product.contract.storage;

namespace product::contract::revision {

void submit(
   forge::contract::context& context,
   const chain::begin_revision& request
) {
   const auto size = storage::checked_size(request);
   forge::contract::check(size.has_value(), "revision size overflow");

   revisions rows{context.get_self(), request.workspace.value};
   rows.emplace(context.get_self(), [&](auto& row) {
      row.id = rows.available_primary_key();
      row.workspace = request.workspace;
      row.inode = request.inode;
      row.size = *size;
   });
}

} // namespace product::contract::revision
