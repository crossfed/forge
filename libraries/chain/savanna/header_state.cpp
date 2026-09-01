module;

#include <forge/exceptions/macros.hpp>

#include <boost/describe.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

module forge.chain.savanna.header_state;

import forge.chain.savanna.genesis;
import forge.chain.protocol.block;
import forge.chain.savanna.finality_core;
import forge.crypto.digest.sha256;
import forge.raw.raw;

namespace forge::chain::savanna {
namespace {

constexpr std::uint32_t producer_repetitions = 12;

struct level_3_commitments {
   digest reversible_blocks_root;
   block_num latest_qc = 0;
   digest latest_qc_finality_digest;
   block_timestamp latest_qc_timestamp;
   block_timestamp timestamp;
   digest base;
};

struct level_2_commitments {
   digest last_pending_finalizer_digest;
   block_timestamp last_pending_finalizer_start;
   digest level_3;
};

struct finality_digest_data {
   std::uint32_t major = 1;
   std::uint32_t minor = 0;
   std::uint32_t active_finalizers = 0;
   std::uint32_t pending_finalizers = 0;
   digest finality_tree;
   digest level_2;
};

BOOST_DESCRIBE_STRUCT(level_3_commitments, (),
                      (reversible_blocks_root, latest_qc, latest_qc_finality_digest, latest_qc_timestamp, timestamp,
                       base))
BOOST_DESCRIBE_STRUCT(level_2_commitments, (), (last_pending_finalizer_digest, last_pending_finalizer_start, level_3))
BOOST_DESCRIBE_STRUCT(finality_digest_data, (),
                      (major, minor, active_finalizers, pending_finalizers, finality_tree, level_2))

digest hash(const auto& value) {
   return forge::crypto::digest::sha256::hash(forge::raw::pack(value));
}

std::uint32_t round_start(block_timestamp timestamp) {
   return timestamp.slot - timestamp.slot % producer_repetitions;
}

bool same_round(block_timestamp next, block_timestamp current) {
   return next.slot < round_start(current) + producer_repetitions;
}

bool first_in_round(block_timestamp current, block_timestamp parent) {
   return parent.slot < round_start(current);
}

std::optional<std::uint32_t> prior_round_start(block_timestamp timestamp) {
   if (timestamp.slot < producer_repetitions) {
      return std::nullopt;
   }
   return round_start(timestamp) - producer_repetitions;
}

const proposer_policy& active_proposers_at(const header_state& previous, block_timestamp timestamp) {
   if (timestamp <= previous.header.timestamp) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_header, "Savanna block timestamp does not increase");
   }
   if (same_round(timestamp, previous.header.timestamp) ||
       (!previous.proposed_proposers && !previous.pending_proposers)) {
      return previous.active_proposers;
   }
   const auto prior = prior_round_start(previous.header.timestamp);
   const auto finalized = block_timestamp{previous.finality.last_final_block_slot()};
   if (previous.proposed_proposers && prior && previous.proposed_proposers->proposal_time.slot < *prior &&
       previous.proposed_proposers->proposal_time <= finalized) {
      return *previous.proposed_proposers;
   }
   if (previous.pending_proposers && previous.pending_proposers->proposal_time <= finalized) {
      return *previous.pending_proposers;
   }
   return previous.active_proposers;
}

const finalizer_policy_state& find_finalizer_policy(const header_state& state, std::uint32_t generation) {
   if (state.active_finalizers.policy.generation == generation) {
      return state.active_finalizers;
   }
   if (state.pending_finalizers && state.pending_finalizers->second.policy.generation == generation) {
      return state.pending_finalizers->second;
   }
   if (state.latest_qc_finalizers && state.latest_qc_finalizers->policy.generation == generation) {
      return *state.latest_qc_finalizers;
   }
   const auto proposed = std::ranges::find_if(state.proposed_finalizers, [generation](const auto& item) {
      return item.second.policy.generation == generation;
   });
   if (proposed != state.proposed_finalizers.end()) {
      return proposed->second;
   }
   FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna QC references an unavailable finalizer policy");
}

void promote_proposers(const header_state& previous, header_state& next) {
   const auto& active = active_proposers_at(previous, next.header.timestamp);
   if (active.proposers.version != next.active_proposers.proposers.version) {
      next.active_proposers = active;
      if (next.proposed_proposers && next.proposed_proposers->proposers.version == active.proposers.version) {
         next.proposed_proposers.reset();
         next.pending_proposers.reset();
      } else if (next.pending_proposers && next.pending_proposers->proposers.version == active.proposers.version) {
         next.pending_proposers.reset();
      }
   }
   if (first_in_round(next.header.timestamp, previous.header.timestamp) && next.proposed_proposers &&
       !next.pending_proposers) {
      next.pending_proposers = std::move(next.proposed_proposers);
      next.proposed_proposers.reset();
   }
}

void promote_finalizers(const header_state& previous, header_state& next) {
   const auto finalized = next.finality.last_final_block_num();
   auto pending_open = true;
   if (previous.pending_finalizers) {
      if (previous.pending_finalizers->first <= finalized) {
         next.latest_qc_finalizers = previous.active_finalizers;
         next.active_finalizers = previous.pending_finalizers->second;
      } else {
         next.pending_finalizers = previous.pending_finalizers;
         pending_open = false;
      }
   }
   if (previous.proposed_finalizers.empty()) {
      return;
   }

   const auto first_reversible =
       std::ranges::find_if(previous.proposed_finalizers, [&](const auto& item) { return item.first > finalized; });
   const auto target = first_reversible == previous.proposed_finalizers.begin() ? previous.proposed_finalizers.end()
                                                                                : std::prev(first_reversible);
   if (target == previous.proposed_finalizers.end()) {
      next.proposed_finalizers = previous.proposed_finalizers;
      return;
   }
   if (pending_open) {
      next.pending_finalizers = std::pair{next.num(), target->second};
      next.last_pending_finalizer_start = next.header.timestamp;
   } else {
      next.proposed_finalizers.push_back(*target);
   }
   next.proposed_finalizers.insert(next.proposed_finalizers.end(), first_reversible,
                                   previous.proposed_finalizers.end());
}

} // namespace

const finalizer_policy_state& finalizer_policy_for(const header_state& state, std::uint32_t generation) {
   return find_finalizer_policy(state, generation);
}

block_num header_state::num() const {
   return forge::chain::protocol::calculate_block_num_from_id(id);
}

const finalizer_policy_state& header_state::last_proposed_finalizers() const {
   if (!proposed_finalizers.empty()) {
      return proposed_finalizers.back().second;
   }
   return last_pending_finalizers();
}

const finalizer_policy_state& header_state::last_pending_finalizers() const {
   return pending_finalizers ? pending_finalizers->second : active_finalizers;
}

const proposer_policy& header_state::last_proposed_proposers() const {
   if (proposed_proposers) {
      return *proposed_proposers;
   }
   if (pending_proposers) {
      return *pending_proposers;
   }
   return active_proposers;
}

digest header_state::base_digest() const {
   auto encoder = forge::crypto::digest::sha256::encoder{};
   forge::raw::pack(encoder, header);
   finality.pack_for_digest(encoder);
   auto proposed = std::vector<std::pair<block_num, forge::chain::savanna::finalizer_policy>>{};
   proposed.reserve(proposed_finalizers.size());
   for (const auto& [num, value] : proposed_finalizers) {
      proposed.emplace_back(num, value.policy);
   }
   auto pending = std::optional<std::pair<block_num, forge::chain::savanna::finalizer_policy>>{};
   if (pending_finalizers) {
      pending.emplace(pending_finalizers->first, pending_finalizers->second.policy);
   }
   forge::raw::pack(encoder, proposed);
   forge::raw::pack(encoder, pending);
   forge::raw::pack(encoder, active_proposers);
   forge::raw::pack(encoder, proposed_proposers);
   forge::raw::pack(encoder, pending_proposers);
   forge::raw::pack(encoder, activated_protocol_features);
   return encoder.result();
}

digest header_state::finality_digest() const {
   const auto latest = finality.latest_qc_claim().block;
   const auto reference =
       finality.is_genesis() ? forge::chain::savanna::block_ref{} : finality.get_block_reference(latest);
   const auto level3 = hash(level_3_commitments{
       .reversible_blocks_root = finality.reversible_blocks_root(),
       .latest_qc = latest,
       .latest_qc_finality_digest = reference.finality_digest,
       .latest_qc_timestamp = block_timestamp{reference.slot},
       .timestamp = header.timestamp,
       .base = base_digest(),
   });
   const auto level2 = hash(level_2_commitments{
       .last_pending_finalizer_digest = last_pending_finalizer_digest,
       .last_pending_finalizer_start = last_pending_finalizer_start,
       .level_3 = level3,
   });
   return hash(finality_digest_data{
       .active_finalizers = active_finalizers.policy.generation,
       .pending_finalizers = last_pending_finalizers().policy.generation,
       .finality_tree = header.action_mroot,
       .level_2 = level2,
   });
}

forge::chain::savanna::block_ref header_state::make_block_ref() const {
   return {
       .num = num(),
       .id = id,
       .slot = header.timestamp.slot,
       .finality_digest = finality_digest(),
       .active_policy_generation = active_finalizers.policy.generation,
       .pending_policy_generation = pending_finalizers ? pending_finalizers->second.policy.generation : 0U,
   };
}

header_state make_genesis_state(const genesis& value, const forge::chain::protocol::signed_block& block) {
   validate(value);
   const auto& header = static_cast<const forge::chain::protocol::block_header&>(block);
   const auto id = forge::chain::protocol::calculate_block_id(header);
   auto result = header_state{
       .id = id,
       .header = header,
       .activated_protocol_features = value.protocol_features,
       .block = {.num = forge::chain::protocol::calculate_block_num(header), .id = id, .slot = header.timestamp.slot},
       .finality = forge::chain::savanna::finality_core::genesis(forge::chain::protocol::calculate_block_num(header),
                                                                 header.timestamp.slot),
       .active_finalizers = {.policy = value.finalizers, .proofs = value.finalizer_proofs},
       .finalizer_generation = value.finalizers.generation,
       .last_pending_finalizer_start = header.timestamp,
       .active_proposers = {.proposal_time = header.timestamp, .proposers = value.proposers},
   };
   result.last_pending_finalizer_digest = hash(result.active_finalizers.policy);
   result.block = result.make_block_ref();
   return result;
}

const forge::chain::protocol::producer_authority& scheduled_producer(const header_state& previous,
                                                                     block_timestamp timestamp) {
   const auto& schedule = active_proposers_at(previous, timestamp).proposers.producers;
   if (schedule.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_header, "Savanna active proposer schedule is empty");
   }
   const auto index = (timestamp.slot % (schedule.size() * producer_repetitions)) / producer_repetitions;
   return schedule[index];
}

std::pair<forge::chain::savanna::verified_finalizer_policy,
          std::optional<forge::chain::savanna::verified_finalizer_policy>>
finalizer_policies(const header_state& state, block_num block) {
   if (state.finality.is_genesis_block_num(block)) {
      return {validate(state.active_finalizers), std::nullopt};
   }
   const auto& reference = state.finality.get_block_reference(block);
   auto active = validate(finalizer_policy_for(state, reference.active_policy_generation));
   auto pending = std::optional<forge::chain::savanna::verified_finalizer_policy>{};
   if (reference.pending_policy_generation != 0U) {
      pending = validate(finalizer_policy_for(state, reference.pending_policy_generation));
   }
   return {std::move(active), std::move(pending)};
}

std::pair<forge::chain::savanna::verified_finalizer_policy,
          std::optional<forge::chain::savanna::verified_finalizer_policy>>
current_finalizer_policies(const header_state& state) {
   auto pending =
       state.pending_finalizers
           ? std::optional<forge::chain::savanna::verified_finalizer_policy>{validate(state.pending_finalizers->second)}
           : std::nullopt;
   return {validate(state.active_finalizers), std::move(pending)};
}

header_state transition(const header_state& previous, const forge::chain::protocol::signed_block_header& signed_header,
                        const header_extensions& extensions) {
   const auto& header = static_cast<const forge::chain::protocol::block_header&>(signed_header);
   if (header.previous != previous.id || header.confirmed != 0U ||
       header.schedule_version != proper_savanna_schedule_version || header.new_producers) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_header, "Savanna block header has invalid parent or legacy fields");
   }
   const auto& producer = scheduled_producer(previous, header.timestamp);
   if (header.producer != producer.producer_name) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_header, "Savanna block was produced by an unexpected proposer");
   }

   auto next = header_state{
       .id = forge::chain::protocol::calculate_block_id(header),
       .header = header,
       .activated_protocol_features = previous.activated_protocol_features,
       .active_finalizers = previous.active_finalizers,
       .finalizer_generation = previous.finalizer_generation,
       .last_pending_finalizer_digest = previous.last_pending_finalizer_digest,
       .last_pending_finalizer_start = previous.last_pending_finalizer_start,
       .active_proposers = previous.active_proposers,
       .proposed_proposers = previous.proposed_proposers,
       .pending_proposers = previous.pending_proposers,
   };
   next.activated_protocol_features.insert(next.activated_protocol_features.end(), extensions.protocol_features.begin(),
                                           extensions.protocol_features.end());
   next.finality = previous.finality.next(previous.make_block_ref(), extensions.finality.claim);
   promote_proposers(previous, next);
   promote_finalizers(previous, next);

   if (extensions.finality.proposers) {
      next.proposed_proposers = apply(previous.last_proposed_proposers(), *extensions.finality.proposers);
   }
   if (extensions.finality.finalizers.has_value() != extensions.finalizer_proofs.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_extension,
                            "Savanna finalizer policy diff and proof extension must appear together");
   }
   if (extensions.finality.finalizers) {
      if (extensions.finalizer_proofs->generation != extensions.finality.finalizers->generation) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_extension,
                               "Savanna finalizer proof generation does not match policy diff");
      }
      auto proposed = apply(previous.last_proposed_finalizers(), *extensions.finality.finalizers,
                            extensions.finalizer_proofs->inserted_proofs);
      next.finalizer_generation = proposed.policy.generation;
      next.proposed_finalizers.emplace_back(next.num(), std::move(proposed));
   }
   next.last_pending_finalizer_digest = hash(next.last_pending_finalizers().policy);
   next.block = next.make_block_ref();
   return next;
}

} // namespace forge::chain::savanna
