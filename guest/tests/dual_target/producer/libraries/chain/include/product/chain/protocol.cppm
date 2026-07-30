module;

#include <cstdint>
#include <optional>

export module product.chain.protocol;

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

   [[nodiscard]] std::uint64_t primary_key() const {
      return id;
   }

   [[nodiscard]] forge::chain::protocol::uint128_t by_workspace_inode() const {
      return (static_cast<forge::chain::protocol::uint128_t>(workspace.value) << 64U) | inode.value;
   }
};

[[nodiscard]] std::optional<std::uint64_t> checked_add(std::uint64_t left, std::uint64_t right);

} // namespace product::chain
