module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

module forge.chain.savanna.admission;

import forge.chain.protocol.block;
import forge.chain.savanna.qc;
import forge.chain.savanna.rank;
import forge.chain.savanna.validation;
import forge.crypto.asymmetric;
import forge.raw.raw;

namespace forge::chain::savanna {
namespace {

void verify_producer(const forge::chain::protocol::signed_block& block,
                     const forge::chain::protocol::producer_authority& producer, const block_extensions& extensions,
                     const block_id& id) {
   const auto* authority = std::get_if<forge::chain::protocol::block_signing_authority_v0>(&producer.authority);
   if (!authority || authority->threshold == 0U) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_header, "Savanna producer signing authority is unsupported or empty");
   }

   auto signatures = extensions.additional_signatures;
   signatures.insert(signatures.begin(), block.producer_signature);
   auto recovered = std::vector<forge::chain::protocol::public_key>{};
   recovered.reserve(signatures.size());
   for (const auto& signature : signatures) {
      auto key = forge::crypto::asymmetric::recover(signature, id);
      if (std::ranges::any_of(recovered, [&](const auto& existing) { return existing == key; })) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_header, "Savanna producer signature key is duplicated");
      }
      recovered.push_back(std::move(key));
   }

   auto weight = std::uint64_t{};
   for (const auto& key : recovered) {
      const auto found = std::ranges::find_if(authority->keys, [&](const auto& item) { return item.key == key; });
      if (found == authority->keys.end()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_header, "Savanna block contains an unauthorized producer signature");
      }
      if (found->weight > std::numeric_limits<std::uint64_t>::max() - weight) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_header, "Savanna producer signature weight overflows");
      }
      weight += found->weight;
   }
   if (weight < authority->threshold) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_header, "Savanna producer signature threshold is not met");
   }
}

} // namespace

genesis_candidate make_genesis_candidate(const genesis& value, state_commitment commitment) {
   validate(value);
   auto block = forge::chain::protocol::signed_block{};
   block.timestamp = value.timestamp;
   block.producer = value.proposers.producers.front().producer_name;
   block.confirmed = 0;
   block.schedule_version = proper_savanna_schedule_version;
   const auto claim = forge::chain::savanna::qc_claim{.block = 1U, .strong = false};
   block.header_extensions.emplace_back(finality_extension_id, forge::raw::pack(finality_extension{.claim = claim}));
   block.header_extensions.emplace_back(state_commitment_extension_id, forge::raw::pack(commitment));
   block.transaction_mroot = forge::chain::protocol::calculate_transaction_mroot(block.transactions);

   auto state = make_genesis_state(value, block);
   auto validation = forge::chain::savanna::make_validation(forge::chain::savanna::validation_leaf{
       .num = state.num(),
       .slot = state.header.timestamp.slot,
       .parent_slot = 0U,
       .finality_digest = state.finality_digest(),
       .commitment = {},
   });
   auto candidate = forge::chain::savanna::candidate{
       .id = state.id,
       .previous = state.header.previous,
       .num = state.num(),
       .timestamp = state.header.timestamp,
       .fork_rank = forge::chain::savanna::make_rank(state.finality, state.block),
       .state = std::move(state),
       .validation = std::move(validation),
   };
   return {.block = std::move(block), .value = std::move(candidate)};
}

prepared_admission prepare(const candidate& parent, const forge::chain::protocol::signed_block& block) {
   const auto& header = static_cast<const forge::chain::protocol::signed_block_header&>(block);
   const auto id = forge::chain::protocol::calculate_block_id(header);
   if (header.previous != parent.id) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_header, "Savanna block does not extend the supplied parent");
   }

   auto decoded_header = decode_header_extensions(header.header_extensions);
   auto decoded_block = decode_block_extensions(block.block_extensions);
   const auto& producer = scheduled_producer(parent.state, header.timestamp);
   if (decoded_header.finality.claim == parent.state.finality.latest_qc_claim() && decoded_block.certificate) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna block provides a redundant QC for an unchanged claim");
   }
   if (decoded_block.certificate) {
      const auto block_num = decoded_block.certificate->block;
      static_cast<void>(block_num == parent.state.finality.current_block_num()
                            ? current_finalizer_policies(parent.state)
                            : finalizer_policies(parent.state, block_num));
   }

   const auto metadata = parent.state.finality.next_metadata(decoded_header.finality.claim);
   const auto expected_root = parent.state.finality.is_genesis_block_num(metadata.latest_qc)
                                  ? digest{}
                                  : forge::chain::savanna::root_at(parent.validation, metadata.latest_qc);
   return {
       .id = id,
       .block = std::move(decoded_block),
       .header = std::move(decoded_header),
       .producer = producer,
       .expected_action_root = expected_root,
   };
}

void verify_merkle(const forge::chain::protocol::signed_block& block, const prepared_admission& prepared) {
   if (forge::chain::protocol::calculate_transaction_mroot(block.transactions) != block.transaction_mroot) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_header,
                            "Savanna transaction receipt Merkle root does not match block header");
   }
   if (block.action_mroot != prepared.expected_action_root) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_merkle,
                            "Savanna finality Merkle root claim does not match parent state");
   }
}

void verify_signature(const forge::chain::protocol::signed_block& block, const prepared_admission& prepared) {
   verify_producer(block, prepared.producer, prepared.block, prepared.id);
}

void verify_qc(const candidate& parent, const prepared_admission& prepared) {
   const auto previous_claim = parent.state.finality.latest_qc_claim();
   if (prepared.header.finality.claim == previous_claim) {
      if (prepared.block.certificate) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna block provides a redundant QC for an unchanged claim");
      }
      return;
   }

   if (prepared.block.certificate) {
      const auto& certificate = *prepared.block.certificate;
      if (certificate.claim() != prepared.header.finality.claim) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna QC does not match header claim");
      }
      const auto current = parent.state.finality.current_block_num();
      auto policies = certificate.block == current ? current_finalizer_policies(parent.state)
                                                   : finalizer_policies(parent.state, certificate.block);
      auto finality_digest = digest{};
      if (certificate.block == current) {
         finality_digest = parent.state.make_block_ref().finality_digest;
      } else if (parent.state.finality.is_genesis_block_num(certificate.block)) {
         finality_digest = parent.state.finality.digest_for_finality();
      } else {
         finality_digest = parent.state.finality.get_block_reference(certificate.block).finality_digest;
      }
      static_cast<void>(forge::chain::savanna::verify(certificate, policies.first, policies.second, finality_digest));
   } else if (prepared.header.finality.claim > previous_claim) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna advancing QC claim has no certificate");
   }
}

candidate finish(const candidate& parent, const forge::chain::protocol::signed_block& block,
                 prepared_admission prepared) {
   const auto& header = static_cast<const forge::chain::protocol::signed_block_header&>(block);
   auto state = transition(parent.state, header, prepared.header);
   return {
       .id = prepared.id,
       .previous = header.previous,
       .num = forge::chain::protocol::calculate_block_num(header),
       .timestamp = header.timestamp,
       .fork_rank = forge::chain::savanna::make_rank(state.finality, state.block),
       .state = std::move(state),
       .validation = parent.validation,
   };
}

candidate admit(const candidate& parent, const forge::chain::protocol::signed_block& block) {
   auto prepared = prepare(parent, block);
   verify_merkle(block, prepared);
   verify_signature(block, prepared);
   verify_qc(parent, prepared);
   return finish(parent, block, std::move(prepared));
}

} // namespace forge::chain::savanna
