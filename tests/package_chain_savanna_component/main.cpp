#include <array>
#include <cstdint>
#include <span>

import forge.chain.savanna.finality_core;
import forge.chain.savanna.finalizer_safety;
import forge.chain.savanna.policy;
import forge.chain.savanna.rank;
import forge.chain.savanna.validation;
import forge.chain.savanna.vote;
import forge.chain.savanna.vote_accumulator;

int main() {
   using namespace forge::chain;
   auto id = core::digest{};
   id._hash[0] = 1U;
   const auto seed = std::array<std::uint8_t, 32>{1U};
   const auto key = forge::crypto::bls::private_key{std::span<const std::uint8_t>{seed}};

   const auto policy = savanna::finalizer_policy{
       .generation = 1U,
       .threshold = 1U,
       .finalizers =
           {
               {
                   .description = "package",
                   .weight = 1U,
                   .public_key = key.get_public_key(),
               },
           },
   };
   const auto verified = savanna::validate(policy, std::array{key.proof_of_possession()});

   const auto finality = savanna::finality_core::genesis(1U, 1U);
   const auto block = savanna::block_ref{
       .num = 1U,
       .id = id,
       .slot = 1U,
       .active_policy_generation = 1U,
   };
   const auto rank = savanna::make_rank(finality, block);
   auto validation = savanna::make_validation({
       .num = 1U,
       .slot = 1U,
       .finality_digest = id,
       .commitment = id,
   });
   auto votes = savanna::vote_accumulator{block, verified};
   const auto safety = savanna::make_finalizer_safety(block);
   const auto message =
       savanna::message_for_vote(id, savanna::vote_kind::strong);
   return verified.get().threshold == 1U && rank.block == 1U &&
                  validation.retained_size() == 1U &&
                  !votes.status().quorum_reached() &&
                  safety.lock().id == id && message.size() == 32U
              ? 0
              : 1;
}
