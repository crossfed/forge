module;

#include <cstdint>
#include <optional>
#include <product/chain/constants.hpp>

export module product.chain.protocol;

import product.chain.limits;

export import forge.chain.protocol.action;
export import product.chain.values;

export namespace product::chain {

struct begin_revision {
   workspace_id workspace;
   inode_id inode;
   std::uint64_t size = 0;

   static constexpr forge::chain::protocol::action_name get_name() {
      return forge::chain::protocol::make_name("beginrev");
   }
};

struct revision {
   std::uint64_t id = 0;
   workspace_id workspace;
   inode_id inode;
   std::uint64_t size = 0;
   revision_state state = revision_state::preparing;

   static constexpr forge::chain::protocol::table_name get_table_name() {
      return forge::chain::protocol::make_name("revisions");
   }

   [[nodiscard]] std::uint64_t primary_key() const {
      return id;
   }

   [[nodiscard]] forge::chain::protocol::uint128_t by_workspace_inode() const {
      return (static_cast<forge::chain::protocol::uint128_t>(workspace.value) << 64U) |
             inode.value;
   }
};

struct unused_audit_record {
   std::uint64_t id = 0;

   static constexpr forge::chain::protocol::table_name get_table_name() {
      return forge::chain::protocol::make_name("unusedaudit");
   }
};

[[nodiscard]] std::optional<std::uint64_t>
checked_add(std::uint64_t left, std::uint64_t right);

[[nodiscard]] inline bool supports_nonzero_sizes() {
   return is_supported_size(PRODUCT_CHAIN_DEFAULT_BLOCK_SIZE);
}

} // namespace product::chain
