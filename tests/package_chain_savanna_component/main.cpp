#include <array>
#include <cstdint>
#include <span>

import forge.chain.savanna.finality_core;
import forge.chain.savanna.policy;
import forge.chain.savanna.rank;

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
   savanna::validate(policy);

   const auto finality = savanna::finality_core::genesis(1U, 1U);
   const auto block = savanna::block_ref{
       .num = 1U,
       .id = id,
       .slot = 1U,
   };
   const auto rank = savanna::make_rank(finality, block);
   return policy.threshold == 1U && rank.block == 1U ? 0 : 1;
}
