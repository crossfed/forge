module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

module forge.chain.savanna.finality_core;

import forge.chain.core.merkle;
import forge.crypto.digest.sha256;
import forge.raw.raw;

namespace forge::chain::savanna {
namespace {

void require(bool condition, const char* message) {
   if (!condition) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality_state, message);
   }
}

std::pair<block_num_t, block_num_t> next_boundaries(const finality_core& core, qc_claim recent) {
   require(recent.block <= core.current_block_num(), "Savanna QC claim is newer than the current block");
   require(core.latest_qc_claim() <= recent, "Savanna QC claim regresses finality state");

   if (!recent.strong) {
      return {core.last_final_block_num(), core.links.front().source};
   }

   const auto& link = core.get_qc_link_from(recent.block);
   return {link.target, link.source};
}

} // namespace

finality_core finality_core::genesis(block_num_t num, block_slot_t slot) {
   return finality_core{
       .links = {{.source = num, .target = num, .strong = false}},
       .refs = {},
       .genesis_slot = slot,
   };
}

bool finality_core::is_genesis() const noexcept {
   return refs.empty();
}

block_num_t finality_core::current_block_num() const {
   require(!links.empty(), "Savanna finality core has no QC links");
   return links.back().source;
}

block_num_t finality_core::last_final_block_num() const {
   require(!links.empty(), "Savanna finality core has no QC links");
   return links.front().target;
}

block_slot_t finality_core::last_final_block_slot() const {
   return is_genesis() ? genesis_slot : get_block_reference(last_final_block_num()).slot;
}

qc_claim finality_core::latest_qc_claim() const {
   require(!links.empty(), "Savanna finality core has no QC links");
   return {.block = links.back().target, .strong = links.back().strong};
}

block_slot_t finality_core::latest_qc_block_slot() const {
   return is_genesis() ? genesis_slot : get_block_reference(latest_qc_claim().block).slot;
}

bool finality_core::extends(block_num_t num, block_id id) const {
   if (num < last_final_block_num() || num >= current_block_num()) {
      return false;
   }
   return get_block_reference(num).id == id;
}

bool finality_core::is_genesis_block_num(block_num_t candidate) const {
   require(last_final_block_num() <= candidate && candidate <= current_block_num(),
           "Savanna candidate block number lies outside finality core");
   return links.front().source == links.front().target && links.front().source == candidate;
}

const block_ref& finality_core::get_block_reference(block_num_t num) const {
   require(last_final_block_num() <= num && num < current_block_num(),
           "Savanna block reference lies outside reversible range");
   const auto index = static_cast<std::size_t>(num - last_final_block_num());
   require(index < refs.size(), "Savanna finality block reference is missing");
   return refs[index];
}

digest finality_core::reversible_blocks_root() const {
   validate(*this);
   if (refs.size() <= 1U) {
      return {};
   }

   auto leaves = std::vector<digest>{};
   leaves.reserve(refs.size() - 1U);
   for (auto index = std::size_t{1}; index < refs.size(); ++index) {
      const auto value = block_ref_digest_data{
          .num = refs[index].num,
          .slot = refs[index].slot,
          .finality_digest = refs[index].finality_digest,
          .parent_slot = refs[index - 1U].slot,
      };
      leaves.push_back(forge::crypto::digest::sha256::hash(value));
   }
   return forge::chain::core::calculate_merkle_root(leaves);
}

const qc_link& finality_core::get_qc_link_from(block_num_t num) const {
   require(!links.empty(), "Savanna finality core has no QC links");
   require(links.front().source <= num && num <= current_block_num(), "Savanna QC link lies outside finality core");
   const auto index = static_cast<std::size_t>(num - links.front().source);
   require(index < links.size(), "Savanna QC link is missing");
   return links[index];
}

finality_metadata finality_core::next_metadata(qc_claim recent) const {
   const auto [last_final, first_source] = next_boundaries(*this, recent);
   static_cast<void>(first_source);
   return {.last_final = last_final, .latest_qc = recent.block};
}

finality_core finality_core::next(const block_ref& current, qc_claim recent) const {
   validate(*this);
   require(!current.empty(), "Savanna current block reference is empty");
   require(current.num == current_block_num(), "Savanna new block reference does not match finality core head");
   if (refs.empty()) {
      require(current.slot == genesis_slot, "Savanna genesis block reference slot does not match the core");
   } else {
      require(refs.back().num != std::numeric_limits<block_num_t>::max() && refs.back().num + 1U == current.num,
              "Savanna new block reference is not contiguous");
      require(refs.back().slot < current.slot, "Savanna new block slot does not increase");
   }
   require(current_block_num() != std::numeric_limits<block_num_t>::max(), "Savanna finality block number overflows");

   const auto [new_last_final, new_first_source] = next_boundaries(*this, recent);
   const auto link_index = static_cast<std::size_t>(new_first_source - links.front().source);
   require(link_index < links.size(), "Savanna new QC boundary does not exist");

   auto result = finality_core{};
   result.links.assign(links.cbegin() + static_cast<std::ptrdiff_t>(link_index), links.cend());
   result.links.push_back({
       .source = static_cast<block_num_t>(current_block_num() + 1U),
       .target = recent.block,
       .strong = recent.strong,
   });

   const auto ref_index = static_cast<std::size_t>(new_last_final - last_final_block_num());
   require(refs.empty() ? ref_index == 0U : ref_index < refs.size(),
           "Savanna new finality boundary has no block reference");
   result.refs.assign(refs.cbegin() + static_cast<std::ptrdiff_t>(ref_index), refs.cend());
   result.refs.push_back(current);
   result.genesis_slot = genesis_slot;
   validate(result);
   return result;
}

digest finality_core::digest_for_finality() const {
   validate(*this);
   auto encoder = forge::crypto::digest::sha256::encoder{};
   pack_for_digest(encoder);
   return encoder.result();
}

void validate(const finality_core& core) {
   require(!core.links.empty(), "Savanna finality core has no QC links");
   for (auto index = std::size_t{}; index < core.links.size(); ++index) {
      const auto& link = core.links[index];
      require(link.target <= link.source, "Savanna QC link target is newer than its source");
      if (index != 0U) {
         const auto& previous = core.links[index - 1U];
         require(previous.source != std::numeric_limits<block_num_t>::max() && previous.source + 1U == link.source,
                 "Savanna QC links are not contiguous");
         require(previous.target <= link.target, "Savanna QC link targets regress");
      }
   }

   if (core.refs.empty()) {
      require(core.links.size() == 1U && core.links.front().source == core.links.front().target,
              "Savanna genesis finality core is inconsistent");
      return;
   }

   const auto last_final = core.links.front().target;
   const auto current = core.links.back().source;
   require(core.links.front().source <= core.links.back().target,
           "Savanna latest QC predates the retained link boundary");
   require(core.refs.front().num == last_final, "Savanna first block reference is not the last final block");
   require(current >= last_final && static_cast<std::uint64_t>(current - last_final) == core.refs.size(),
           "Savanna reversible block reference count is inconsistent");

   for (auto index = std::size_t{}; index < core.refs.size(); ++index) {
      const auto& ref = core.refs[index];
      require(!ref.empty(), "Savanna finality core contains an empty block reference");
      if (index != 0U) {
         const auto& previous = core.refs[index - 1U];
         require(previous.num != std::numeric_limits<block_num_t>::max() && previous.num + 1U == ref.num,
                 "Savanna block references are not contiguous");
         require(previous.slot < ref.slot, "Savanna block reference slots do not increase");
      }
   }
   require(core.refs.back().num != std::numeric_limits<block_num_t>::max() && core.refs.back().num + 1U == current,
           "Savanna finality core head does not follow its references");
}

} // namespace forge::chain::savanna
