export module product.contract.revision;

import forge.contract;
import forge.contract.multi_index;
import product.chain.protocol;

export namespace product::contract::revision {

using forge::chain::protocol::literals::operator""_n;

using by_workspace_inode =
   forge::contract::indexed_by<
      "byinode"_n,
      forge::contract::const_mem_fun<
         chain::revision,
         forge::chain::protocol::uint128_t,
         &chain::revision::by_workspace_inode
      >
   >;
using revisions =
   forge::contract::multi_index<
      "revisions"_n,
      chain::revision,
      by_workspace_inode
   >;

void submit(
   forge::contract::context& context,
   const chain::begin_revision& request
);

} // namespace product::contract::revision
