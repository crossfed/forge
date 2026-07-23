module;

#include <forge/exceptions/macros.hpp>

module forge.chain.savanna.rank;

namespace forge::chain::savanna {

rank make_rank(const finality_core& core, const block_ref& block) {
   if (block.empty() || block.num != core.current_block_num()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality_state,
                            "Savanna rank block does not match the finality core head");
   }
   return {
       .latest_qc = core.latest_qc_block_slot(),
       .block = block.slot,
       .id = block.id,
   };
}

bool better(const rank& left, const rank& right) noexcept {
   return left > right;
}

} // namespace forge::chain::savanna
