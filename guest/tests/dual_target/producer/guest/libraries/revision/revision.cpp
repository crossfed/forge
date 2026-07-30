module;

#include <cstdint>

module product.contract.revision;

import forge.contract.multi_index;
import product.contract.storage;

using forge::chain::protocol::literals::operator""_n;

namespace product::contract::revision {

using by_workspace_inode =
    forge::contract::indexed_by<"byinode"_n,
                                forge::contract::const_mem_fun<
                                    chain::revision,
                                    forge::chain::protocol::uint128_t,
                                    &chain::revision::by_workspace_inode>>;
using revisions =
    forge::contract::multi_index<"revisions"_n, chain::revision, by_workspace_inode>;

void submit(forge::contract::context& context, chain::begin_revision const& request) {
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

}
