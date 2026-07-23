module;

#include <forge/exceptions/macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

module forge.chain.savanna.qc;

import forge.chain.quorum.evaluate;

namespace forge::chain::savanna {
namespace {

constexpr auto weak_postfix = std::array<std::uint8_t, 4>{'W', 'E', 'A', 'K'};

std::vector<std::uint8_t> weak_digest(digest value) {
   auto result = std::vector<std::uint8_t>{};
   const auto bytes = value.to_uint8_span();
   result.reserve(bytes.size() + weak_postfix.size());
   result.insert(result.end(), bytes.begin(), bytes.end());
   result.insert(result.end(), weak_postfix.begin(), weak_postfix.end());
   return result;
}

std::vector<std::uint64_t> weights(const verified_finalizer_policy& policy) {
   auto result = std::vector<std::uint64_t>{};
   result.reserve(policy.get().finalizers.size());
   for (const auto& finalizer : policy.get().finalizers) {
      result.push_back(finalizer.weight);
   }
   return result;
}

std::vector<std::uint32_t> signers(const vote_bitset& bits) {
   auto result = std::vector<std::uint32_t>{};
   result.reserve(bits.count());
   for (auto index = bits.find_first(); index != vote_bitset::npos; index = bits.find_next(index)) {
      if (index > std::numeric_limits<std::uint32_t>::max()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna finalizer index exceeds the supported range");
      }
      result.push_back(static_cast<std::uint32_t>(index));
   }
   return result;
}

std::vector<forge::crypto::bls::proof_verified_public_key> selected_keys(const vote_bitset& votes,
                                                                         const verified_finalizer_policy& policy) {
   auto result = std::vector<forge::crypto::bls::proof_verified_public_key>{};
   result.reserve(votes.count());
   for (auto index = votes.find_first(); index != vote_bitset::npos; index = votes.find_next(index)) {
      result.push_back(policy.verified_keys()[index]);
   }
   return result;
}

bool voted(const std::optional<vote_bitset>& votes, std::size_t index) {
   return votes && (*votes)[index];
}

void verify_dual_finalizer_votes(const qc_signature& active_signature, const verified_finalizer_policy& active_policy,
                                 const qc_signature& pending_signature,
                                 const verified_finalizer_policy& pending_policy) {
   const auto& active = active_policy.get();
   const auto& pending = pending_policy.get();
   for (auto active_index = std::size_t{}; active_index < active.finalizers.size(); ++active_index) {
      for (auto pending_index = std::size_t{}; pending_index < pending.finalizers.size(); ++pending_index) {
         if (active.finalizers[active_index].public_key != pending.finalizers[pending_index].public_key) {
            continue;
         }
         if (voted(active_signature.strong_votes, active_index) !=
                 voted(pending_signature.strong_votes, pending_index) ||
             voted(active_signature.weak_votes, active_index) != voted(pending_signature.weak_votes, pending_index)) {
            FORGE_THROW_EXCEPTION(exceptions::invalid_qc,
                                  "Savanna dual finalizer voted differently in active and pending policies");
         }
      }
   }
}

} // namespace

bool qc_signature::weak() const noexcept {
   return weak_votes.has_value();
}

bool qc_signature::strong() const noexcept {
   return !weak();
}

bool quorum_certificate::strong() const noexcept {
   return active.strong() && (!pending || pending->strong());
}

qc_claim quorum_certificate::claim() const noexcept {
   return {.block = block, .strong = strong()};
}

void verify_basic(const qc_signature& value, const verified_finalizer_policy& policy) {
   if (!value.strong_votes && !value.weak_votes) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna QC contains neither strong nor weak votes");
   }

   const auto count = policy.get().finalizers.size();
   if ((value.strong_votes && value.strong_votes->size() != count) ||
       (value.weak_votes && value.weak_votes->size() != count)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna QC vote bitset size does not match finalizer policy");
   }

   if (value.strong_votes && value.weak_votes) {
      auto overlap = *value.strong_votes;
      overlap &= *value.weak_votes;
      if (overlap.any()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna finalizer voted both strong and weak");
      }
   }
}

void verify_weights(const qc_signature& value, const verified_finalizer_policy& policy) {
   verify_basic(value, policy);
   const auto policy_weights = weights(policy);
   const auto strong_signers = value.strong_votes ? signers(*value.strong_votes) : std::vector<std::uint32_t>{};
   const auto weak_signers = value.weak_votes ? signers(*value.weak_votes) : std::vector<std::uint32_t>{};
   const auto strong = forge::chain::quorum::evaluate(policy.get().threshold, policy_weights, strong_signers);
   if (value.strong()) {
      if (!strong.reached()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna strong quorum threshold is not met");
      }
      return;
   }

   auto combined = strong_signers;
   combined.insert(combined.end(), weak_signers.begin(), weak_signers.end());
   const auto weak = forge::chain::quorum::evaluate(policy.get().threshold, policy_weights, combined);
   if (!weak.reached()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna weak quorum threshold is not met");
   }
}

void verify_signature(const qc_signature& value, const verified_finalizer_policy& policy, digest strong_digest) {
   verify_weights(value, policy);

   auto strong_keys = std::vector<forge::crypto::bls::proof_verified_public_key>{};
   auto weak_keys = std::vector<forge::crypto::bls::proof_verified_public_key>{};
   auto weak_message = std::vector<std::uint8_t>{};
   auto groups = std::vector<forge::crypto::bls::aggregate_verification_group>{};
   groups.reserve(2U);

   if (value.strong_votes && value.strong_votes->any()) {
      strong_keys = selected_keys(*value.strong_votes, policy);
      groups.push_back({
          .public_keys = strong_keys,
          .message = strong_digest.to_uint8_span(),
      });
   }
   if (value.weak_votes && value.weak_votes->any()) {
      weak_keys = selected_keys(*value.weak_votes, policy);
      weak_message = weak_digest(strong_digest);
      groups.push_back({
          .public_keys = weak_keys,
          .message = weak_message,
      });
   }

   if (!forge::crypto::bls::verify_grouped(groups, value.signature)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_qc_signature, "Savanna QC signature validation failed");
   }
}

void verify(const quorum_certificate& value, const verified_finalizer_policy& active,
            const std::optional<verified_finalizer_policy>& pending, digest finality_digest) {
   if (value.pending.has_value() != pending.has_value()) {
      if (value.pending) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna QC contains a pending-policy signature unexpectedly");
      }
      FORGE_THROW_EXCEPTION(exceptions::invalid_qc, "Savanna QC omits the pending-policy signature");
   }

   verify_basic(value.active, active);
   if (value.pending) {
      verify_basic(*value.pending, *pending);
      verify_dual_finalizer_votes(value.active, active, *value.pending, *pending);
   }

   verify_signature(value.active, active, finality_digest);
   if (value.pending) {
      verify_signature(*value.pending, *pending, finality_digest);
   }
}

} // namespace forge::chain::savanna
