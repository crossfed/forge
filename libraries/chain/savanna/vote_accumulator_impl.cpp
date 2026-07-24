module;

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

module forge.chain.savanna.vote_accumulator;

#include "details/vote_accumulator_impl.hxx"

namespace forge::chain::savanna {

vote_accumulator::impl::policy_votes::policy_votes(
    verified_finalizer_policy policy_value)
    : policy{std::move(policy_value)},
      strong_votes{policy.get().finalizers.size()},
      weak_votes{policy.get().finalizers.size()} {
   auto total = std::uint64_t{};
   for (auto index = std::size_t{}; index < policy.get().finalizers.size(); ++index) {
      const auto& finalizer = policy.get().finalizers[index];
      indices.emplace(finalizer.public_key, index);
      total += finalizer.weight;
   }
   weak_limit = total - policy.get().threshold;
}

std::optional<std::size_t> vote_accumulator::impl::policy_votes::find(
    const forge::crypto::bls::public_key& key) const {
   const auto iterator = indices.find(key);
   return iterator == indices.end() ? std::nullopt
                                    : std::optional<std::size_t>{iterator->second};
}

std::optional<vote_kind>
vote_accumulator::impl::policy_votes::vote_at(std::size_t index) const {
   if (strong_votes[index]) {
      return vote_kind::strong;
   }
   if (weak_votes[index]) {
      return vote_kind::weak;
   }
   return std::nullopt;
}

void vote_accumulator::impl::policy_votes::add(
    std::size_t index, vote_kind kind,
    const forge::crypto::bls::signature& signature) {
   const auto weight = policy.get().finalizers[index].weight;
   if (kind == vote_kind::strong) {
      strong_votes.set(index);
      strong_signature.aggregate(signature);
      strong_weight += weight;

      switch (state) {
      case accumulator_state::unrestricted:
      case accumulator_state::restricted:
         if (strong_weight >= policy.get().threshold) {
            state = accumulator_state::strong;
         } else if (strong_weight + weak_weight >= policy.get().threshold) {
            state = state == accumulator_state::restricted
                        ? accumulator_state::weak_final
                        : accumulator_state::weak_achieved;
         }
         break;
      case accumulator_state::weak_achieved:
         if (strong_weight >= policy.get().threshold) {
            state = accumulator_state::strong;
         }
         break;
      case accumulator_state::weak_final:
      case accumulator_state::strong:
         break;
      }
      return;
   }

   weak_votes.set(index);
   weak_signature.aggregate(signature);
   weak_weight += weight;

   switch (state) {
   case accumulator_state::unrestricted:
   case accumulator_state::restricted:
      if (strong_weight + weak_weight >= policy.get().threshold) {
         state = accumulator_state::weak_achieved;
      }
      if (weak_weight > weak_limit) {
         if (state == accumulator_state::weak_achieved) {
            state = accumulator_state::weak_final;
         } else if (state == accumulator_state::unrestricted) {
            state = accumulator_state::restricted;
         }
      }
      break;
   case accumulator_state::weak_achieved:
      if (weak_weight > weak_limit) {
         state = accumulator_state::weak_final;
      }
      break;
   case accumulator_state::weak_final:
   case accumulator_state::strong:
      break;
   }
}

std::optional<qc_signature>
vote_accumulator::impl::policy_votes::local() const {
   if (state == accumulator_state::strong) {
      return qc_signature{
          .strong_votes = strong_votes,
          .signature = strong_signature,
      };
   }
   if (state != accumulator_state::weak_achieved &&
       state != accumulator_state::weak_final) {
      return std::nullopt;
   }

   auto signature = strong_signature;
   signature.aggregate(weak_signature);
   return qc_signature{
       .strong_votes = strong_votes,
       .weak_votes = weak_votes,
       .signature = std::move(signature),
   };
}

policy_accumulator_status
vote_accumulator::impl::policy_votes::status() const noexcept {
   return {
       .state = state,
       .strong_weight = strong_weight,
       .weak_weight = weak_weight,
   };
}

vote_accumulator::impl::impl(
    block_ref candidate_value, verified_finalizer_policy active_value,
    std::optional<verified_finalizer_policy> pending_value)
    : candidate{std::move(candidate_value)},
      active{std::move(active_value)},
      pending{pending_value
                  ? std::optional<policy_votes>{policy_votes{std::move(*pending_value)}}
                  : std::nullopt} {}

vote_result vote_accumulator::impl::classify_existing(
    const forge::crypto::bls::public_key& finalizer, vote_kind kind) const {
   auto existing = std::optional<vote_kind>{};
   if (const auto index = active.find(finalizer)) {
      existing = active.vote_at(*index);
   }
   if (pending) {
      if (const auto index = pending->find(finalizer)) {
         const auto pending_vote = pending->vote_at(*index);
         if (existing && pending_vote && *existing != *pending_vote) {
            return vote_result::conflicting;
         }
         if (!existing) {
            existing = pending_vote;
         }
      }
   }
   if (!existing) {
      return vote_result::accepted;
   }
   return *existing == kind ? vote_result::duplicate : vote_result::conflicting;
}

bool vote_accumulator::impl::known(
    const forge::crypto::bls::public_key& finalizer) const {
   return active.find(finalizer).has_value() ||
          (pending && pending->find(finalizer).has_value());
}

void vote_accumulator::impl::add_verified(const finalizer_vote& vote) {
   if (const auto index = active.find(vote.finalizer)) {
      active.add(*index, vote.kind, vote.signature);
   }
   if (pending) {
      if (const auto index = pending->find(vote.finalizer)) {
         pending->add(*index, vote.kind, vote.signature);
      }
   }
}

std::optional<quorum_certificate> vote_accumulator::impl::local() const {
   auto active_signature = active.local();
   if (!active_signature) {
      return std::nullopt;
   }

   auto pending_signature = std::optional<qc_signature>{};
   if (pending) {
      pending_signature = pending->local();
      if (!pending_signature) {
         return std::nullopt;
      }
   }
   return quorum_certificate{
       .block = candidate.num,
       .active = std::move(*active_signature),
       .pending = std::move(pending_signature),
   };
}

bool vote_accumulator::impl::observe(quorum_certificate certificate) {
   if (!received || (!received->strong() && certificate.strong())) {
      received = std::move(certificate);
      return true;
   }
   return false;
}

std::optional<quorum_certificate> vote_accumulator::impl::best() const {
   auto local_certificate = local();
   if (!local_certificate) {
      return received;
   }
   if (!received) {
      return local_certificate;
   }
   if (received->strong() || !local_certificate->strong()) {
      return received;
   }
   return local_certificate;
}

} // namespace forge::chain::savanna
