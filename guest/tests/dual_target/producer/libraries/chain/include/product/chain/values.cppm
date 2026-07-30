module;

#include <cstdint>

export module product.chain.values;

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

} // namespace product::chain
