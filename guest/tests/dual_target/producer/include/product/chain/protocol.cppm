module;

#include <cstdint>
#include <optional>
#include <type_traits>

export module product.chain.protocol;

export import forge.chain.protocol.values;

export namespace product::chain {

struct workspace_id {
   std::uint64_t value = 0;

   auto operator<=>(const workspace_id&) const = default;
};

struct inode_id {
   std::uint64_t value = 0;

   auto operator<=>(const inode_id&) const = default;
};

enum class revision_state : std::uint8_t {
   preparing,
   active,
};

struct begin_revision {
   workspace_id workspace;
   inode_id inode;
   std::uint64_t size = 0;

   static constexpr forge::chain::protocol::action_name get_name() {
      if (std::is_constant_evaluated()) {
         return forge::chain::protocol::make_name("beginrev");
      }
      return forge::chain::protocol::make_name("runtime");
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
[[nodiscard]] const char* implementation_file();

} // namespace product::chain
