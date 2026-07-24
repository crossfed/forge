module;

#include <boost/describe.hpp>

#include <vector>

export module forge.chain.savanna.finality_core;

export import forge.chain.savanna.types;
export import forge.chain.savanna.exceptions;

import forge.raw.raw;

export namespace forge::chain::savanna {

struct qc_link {
   block_num_t source = 0;
   block_num_t target = 0;
   bool strong = false;
};

struct qc_claim {
   block_num_t block = 0;
   bool strong = false;

   auto operator<=>(const qc_claim&) const = default;
};

struct finality_metadata {
   block_num_t last_final = 0;
   block_num_t latest_qc = 0;
};

struct finality_core {
   std::vector<qc_link> links;
   std::vector<block_ref> refs;
   block_slot_t genesis_slot = 0;

   [[nodiscard]] static finality_core genesis(block_num_t num, block_slot_t slot);
   [[nodiscard]] bool is_genesis() const noexcept;
   [[nodiscard]] block_num_t current_block_num() const;
   [[nodiscard]] block_num_t last_final_block_num() const;
   [[nodiscard]] block_slot_t last_final_block_slot() const;
   [[nodiscard]] qc_claim latest_qc_claim() const;
   [[nodiscard]] block_slot_t latest_qc_block_slot() const;
   [[nodiscard]] bool extends(block_num_t num, block_id id) const;
   [[nodiscard]] bool is_genesis_block_num(block_num_t candidate) const;
   [[nodiscard]] const block_ref& get_block_reference(block_num_t num) const;
   [[nodiscard]] digest reversible_blocks_root() const;
   [[nodiscard]] const qc_link& get_qc_link_from(block_num_t num) const;
   [[nodiscard]] finality_metadata next_metadata(qc_claim recent) const;
   [[nodiscard]] finality_core next(const block_ref& current, qc_claim recent) const;
   [[nodiscard]] digest digest_for_finality() const;

   template <typename Stream> void pack_for_digest(Stream& stream) const {
      forge::raw::pack(stream, links);
      forge::raw::pack(stream, forge::unsigned_int{static_cast<std::uint32_t>(refs.size())});
      for (const auto& ref : refs) {
         forge::raw::pack(stream, ref.id);
         forge::raw::pack(stream, ref.slot);
         forge::raw::pack(stream, ref.finality_digest);
      }
      forge::raw::pack(stream, genesis_slot);
   }
};

void validate(const finality_core& core);

BOOST_DESCRIBE_STRUCT(qc_link, (), (source, target, strong))
BOOST_DESCRIBE_STRUCT(qc_claim, (), (block, strong))
BOOST_DESCRIBE_STRUCT(finality_metadata, (), (last_final, latest_qc))
BOOST_DESCRIBE_STRUCT(finality_core, (), (links, refs, genesis_slot))

} // namespace forge::chain::savanna
