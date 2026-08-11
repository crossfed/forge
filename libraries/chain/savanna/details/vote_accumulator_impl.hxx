#pragma once

namespace forge::chain::savanna {

struct vote_accumulator::impl {
   struct policy_votes {
      explicit policy_votes(verified_finalizer_policy policy_value);

      [[nodiscard]] std::optional<std::size_t>
      find(const forge::crypto::bls::public_key& key) const;
      [[nodiscard]] std::optional<forge::crypto::bls::proof_verified_public_key>
      verified(const forge::crypto::bls::public_key& key) const;
      [[nodiscard]] std::optional<vote_kind> vote_at(std::size_t index) const;
      void add(std::size_t index, vote_kind kind,
               const forge::crypto::bls::signature& signature);
      [[nodiscard]] std::optional<qc_signature> local() const;
      [[nodiscard]] policy_accumulator_status status() const noexcept;

      verified_finalizer_policy policy;
      std::map<forge::crypto::bls::public_key, std::size_t> indices;
      std::uint64_t weak_limit = 0;
      std::uint64_t strong_weight = 0;
      std::uint64_t weak_weight = 0;
      accumulator_state state = accumulator_state::unrestricted;
      vote_bitset strong_votes;
      vote_bitset weak_votes;
      forge::crypto::bls::signature_accumulator strong_signature;
      forge::crypto::bls::signature_accumulator weak_signature;
   };

   impl(block_ref candidate_value, verified_finalizer_policy active_value,
        std::optional<verified_finalizer_policy> pending_value);

   [[nodiscard]] vote_result classify_existing(
       const forge::crypto::bls::public_key& finalizer, vote_kind kind) const;
   [[nodiscard]] std::optional<forge::crypto::bls::proof_verified_public_key>
   verified(const forge::crypto::bls::public_key& finalizer) const;
   void add_verified(const finalizer_vote& vote);
   [[nodiscard]] std::optional<quorum_certificate> local() const;
   bool observe(quorum_certificate certificate);
   [[nodiscard]] std::optional<quorum_certificate> best() const;

   block_ref candidate;
   policy_votes active;
   std::optional<policy_votes> pending;
   std::optional<quorum_certificate> received;
   mutable std::mutex mutex;
};

} // namespace forge::chain::savanna
