module;

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

module forge.chain.savanna.vote_accumulator;

#include "details/vote_accumulator_impl.hxx"

namespace forge::chain::savanna {

bool policy_accumulator_status::quorum_reached() const noexcept {
   return state == accumulator_state::weak_achieved ||
          state == accumulator_state::weak_final ||
          state == accumulator_state::strong;
}

bool accumulator_status::quorum_reached() const noexcept {
   return active.quorum_reached() && (!pending || pending->quorum_reached());
}

vote_accumulator::vote_accumulator(
    block_ref candidate, verified_finalizer_policy active,
    std::optional<verified_finalizer_policy> pending)
    : impl_{std::make_unique<impl>(std::move(candidate), std::move(active),
                                  std::move(pending))} {}

vote_accumulator::~vote_accumulator() = default;
vote_accumulator::vote_accumulator(vote_accumulator&&) noexcept = default;
vote_accumulator& vote_accumulator::operator=(vote_accumulator&&) noexcept = default;

vote_result vote_accumulator::add(const finalizer_vote& vote) {
   if (vote.block != impl_->candidate.id) {
      return vote_result::wrong_block;
   }

   {
      auto lock = std::lock_guard{impl_->mutex};
      if (!impl_->known(vote.finalizer)) {
         return vote_result::unknown_finalizer;
      }
      const auto existing = impl_->classify_existing(vote.finalizer, vote.kind);
      if (existing != vote_result::accepted) {
         return existing;
      }
   }

   const auto message = message_for_vote(impl_->candidate.finality_digest, vote.kind);
   if (!forge::crypto::bls::verify(vote.finalizer, message, vote.signature)) {
      return vote_result::invalid_signature;
   }

   auto lock = std::lock_guard{impl_->mutex};
   const auto existing = impl_->classify_existing(vote.finalizer, vote.kind);
   if (existing != vote_result::accepted) {
      return existing;
   }
   impl_->add_verified(vote);
   return vote_result::accepted;
}

bool vote_accumulator::observe(const quorum_certificate& certificate) {
   if (certificate.block != impl_->candidate.num) {
      return false;
   }
   static_cast<void>(verify(certificate, impl_->active.policy,
                            impl_->pending
                                ? std::optional<verified_finalizer_policy>{impl_->pending->policy}
                                : std::nullopt,
                            impl_->candidate.finality_digest));

   auto lock = std::lock_guard{impl_->mutex};
   auto changed = impl_->active.observe(certificate.active);
   if (impl_->pending) {
      changed = impl_->pending->observe(*certificate.pending) || changed;
   }
   return changed;
}

std::optional<quorum_certificate> vote_accumulator::best() const {
   auto lock = std::lock_guard{impl_->mutex};
   auto active = impl_->active.best();
   if (!active) {
      return std::nullopt;
   }

   auto pending = std::optional<qc_signature>{};
   if (impl_->pending) {
      pending = impl_->pending->best();
      if (!pending) {
         return std::nullopt;
      }
   }
   return quorum_certificate{
       .block = impl_->candidate.num,
       .active = std::move(*active),
       .pending = std::move(pending),
   };
}

accumulator_status vote_accumulator::status() const {
   auto lock = std::lock_guard{impl_->mutex};
   return {
       .active = impl_->active.status(),
       .pending = impl_->pending
                      ? std::optional<policy_accumulator_status>{impl_->pending->status()}
                      : std::nullopt,
   };
}

finalizer_vote_status
vote_accumulator::status(const forge::crypto::bls::public_key& finalizer) const {
   auto lock = std::lock_guard{impl_->mutex};
   auto result = finalizer_vote_status{};
   if (const auto index = impl_->active.find(finalizer)) {
      result.known = true;
      result.active = impl_->active.vote_at(*index);
   }
   if (impl_->pending) {
      if (const auto index = impl_->pending->find(finalizer)) {
         result.known = true;
         result.pending = impl_->pending->vote_at(*index);
      }
   }
   return result;
}

} // namespace forge::chain::savanna
