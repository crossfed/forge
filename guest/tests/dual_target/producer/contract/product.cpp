#include "../contract_support/local_value.hpp"
#include <cstdint>
#include <product/chain/version.hpp>

import forge.contract;
import forge.contract.multi_index;
import product.chain.protocol;

using forge::chain::protocol::literals::operator""_n;

static_assert(product::chain::protocol_version == 1);
static_assert(product_contract_local_value == 42);

class [[forge::contract("product")]] product_contract : public forge::contract::context {
 public:
   using context::context;

   using by_workspace_inode = forge::contract::indexed_by<
       "byinode"_n, forge::contract::const_mem_fun<product::chain::revision, forge::chain::protocol::uint128_t,
                                                   &product::chain::revision::by_workspace_inode>>;
   using revisions = forge::contract::multi_index<"revisions"_n, product::chain::revision, by_workspace_inode>;

   [[forge::action]] void beginrev(product::chain::begin_revision request) {
      const auto size = product::chain::checked_add(request.size, 0);
      forge::contract::check(size.has_value(), "revision size overflow");

      revisions rows{get_self(), request.workspace.value};
      rows.emplace(get_self(), [&](auto& row) {
         row.id = rows.available_primary_key();
         row.workspace = request.workspace;
         row.inode = request.inode;
         row.size = *size;
      });
   }
};
