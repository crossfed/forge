module;

#include <forge/exceptions/macros.hpp>

#include <utility>

module forge.chain.savanna.finalizer_safety;

namespace forge::chain::savanna {
namespace {

void require_candidate(const finality_core& core, const block_ref& candidate) {
   validate(core);
   if (candidate.empty() || candidate.num != core.current_block_num()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finalizer_safety_state,
                            "Savanna vote candidate does not match finality core head");
   }
}

} // namespace

const block_ref& finalizer_safety_state::last_vote() const noexcept {
   return last_vote_;
}

const block_ref& finalizer_safety_state::lock() const noexcept {
   return lock_;
}

block_slot_t
finalizer_safety_state::other_branch_latest_slot() const noexcept {
   return other_branch_latest_slot_;
}

void finalizer_safety_state::require_valid() const {
   if (lock_.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finalizer_safety_state,
                            "Savanna finalizer safety lock is empty");
   }
   if (last_vote_.empty()) {
      if (other_branch_latest_slot_ != 0U) {
         FORGE_THROW_EXCEPTION(
             exceptions::invalid_finalizer_safety_state,
             "Savanna finalizer safety has a branch slot without a prior vote");
      }
      return;
   }
   if (last_vote_.slot < lock_.slot ||
       other_branch_latest_slot_ > last_vote_.slot) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finalizer_safety_state,
                            "Savanna finalizer safety slots are inconsistent");
   }
}

finalizer_safety_state make_finalizer_safety(block_ref initial_lock) {
   auto result = finalizer_safety_state{};
   result.lock_ = std::move(initial_lock);
   result.require_valid();
   return result;
}

vote_plan plan_vote(const finalizer_safety_state& state,
                    const finality_core& candidate_core,
                    const block_ref& candidate) {
   state.require_valid();
   require_candidate(candidate_core, candidate);

   auto result = vote_plan{
       .next = state,
   };
   result.monotonic =
       state.last_vote_.empty() || candidate.slot > state.last_vote_.slot;
   if (!result.monotonic) {
      return result;
   }

   const auto latest_qc_slot = candidate_core.latest_qc_block_slot();
   result.live = latest_qc_slot > state.lock_.slot;
   if (!result.live) {
      result.safe =
          candidate_core.extends(state.lock_.num, state.lock_.id);
   }
   if (!result.live && !result.safe) {
      return result;
   }

   if (state.last_vote_.empty() ||
       state.last_vote_.slot <= latest_qc_slot) {
      result.decision = vote_decision::strong;
   } else if (candidate_core.extends(state.last_vote_.num,
                                     state.last_vote_.id)) {
      result.decision =
          state.other_branch_latest_slot_ <= latest_qc_slot
              ? vote_decision::strong
              : vote_decision::weak;
   } else {
      result.decision = vote_decision::weak;
      result.next.other_branch_latest_slot_ = state.last_vote_.slot;
   }

   if (result.decision == vote_decision::strong) {
      result.next.other_branch_latest_slot_ = 0U;
      if (latest_qc_slot > state.lock_.slot) {
         result.next.lock_ = candidate_core.get_block_reference(
             candidate_core.latest_qc_claim().block);
      }
   }
   result.next.last_vote_ = candidate;
   result.next.require_valid();
   return result;
}

finalizer_safety_state advance_from_qc(
    finalizer_safety_state state, const finality_core& candidate_core,
    const block_ref& candidate,
    const verified_quorum_certificate& certificate,
    const forge::crypto::bls::public_key& finalizer) {
   state.require_valid();
   require_candidate(candidate_core, candidate);
   if (certificate.get().block != candidate.num ||
       certificate.finality_digest() != candidate.finality_digest ||
       !certificate.get().strong() ||
       !certificate.has_strong_vote(finalizer)) {
      FORGE_THROW_EXCEPTION(
          exceptions::invalid_qc,
          "Savanna QC does not prove a strong vote by the finalizer");
   }

   const auto latest_qc_slot = candidate_core.latest_qc_block_slot();
   if (!state.last_vote_.empty() &&
       candidate.slot <= state.last_vote_.slot) {
      return state;
   }

   if (latest_qc_slot > state.lock_.slot) {
      state.lock_ = candidate_core.get_block_reference(
          candidate_core.latest_qc_claim().block);
   }
   state.last_vote_ = candidate;
   state.other_branch_latest_slot_ = 0U;
   state.require_valid();
   return state;
}

} // namespace forge::chain::savanna
