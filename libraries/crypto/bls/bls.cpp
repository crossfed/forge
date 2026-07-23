module;
#include <bls12-381/bls12-381.hpp>
#include <cstdint>
#include <span>
#include <vector>

module forge.crypto.bls;

namespace forge::crypto::bls {

bool verify(const public_key& pubkey, std::span<const uint8_t> message, const signature& signature) {
   return bls12_381::verify(pubkey.jacobian_montgomery_le(), message, signature.jacobian_montgomery_le());
};

std::optional<proof_verified_public_key> verify_proof_of_possession(const public_key& key, const signature& proof) {
   if (key == public_key{}) {
      return std::nullopt;
   }
   if (!bls12_381::pop_verify(key.jacobian_montgomery_le(), proof.jacobian_montgomery_le())) {
      return std::nullopt;
   }
   return proof_verified_public_key{key};
}

bool verify_grouped(std::span<const aggregate_verification_group> groups, const aggregate_signature& signature) {
   if (groups.empty()) {
      return false;
   }

   auto public_keys = std::vector<bls12_381::g1>{};
   auto messages = std::vector<std::vector<std::uint8_t>>{};
   public_keys.reserve(groups.size());
   messages.reserve(groups.size());

   for (const auto& group : groups) {
      if (group.public_keys.empty()) {
         return false;
      }

      auto group_keys = std::vector<bls12_381::g1>{};
      group_keys.reserve(group.public_keys.size());
      for (const auto& key : group.public_keys) {
         group_keys.push_back(key.get().jacobian_montgomery_le());
      }
      public_keys.push_back(bls12_381::aggregate_public_keys(group_keys));
      messages.emplace_back(group.message.begin(), group.message.end());
   }

   return bls12_381::aggregate_verify(public_keys, messages, signature.jacobian_montgomery_le(), true);
}

} // namespace forge::crypto::bls
