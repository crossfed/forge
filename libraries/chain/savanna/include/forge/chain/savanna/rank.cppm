module;

#include <boost/describe.hpp>

export module forge.chain.savanna.rank;

export import forge.chain.savanna.finality_core;

export namespace forge::chain::savanna {

struct rank {
   block_slot_t latest_qc = 0;
   block_slot_t block = 0;
   block_id id;

   auto operator<=>(const rank&) const = default;
};

[[nodiscard]] rank make_rank(const finality_core& core, const block_ref& block);
[[nodiscard]] bool better(const rank& left, const rank& right) noexcept;

BOOST_DESCRIBE_STRUCT(rank, (), (latest_qc, block, id))

} // namespace forge::chain::savanna
