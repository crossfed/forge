module;

#include <boost/describe.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstdint>
#include <utility>

export module forge.chain.savanna.finalizer_safety;

export import forge.chain.savanna.finality_core;
export import forge.chain.savanna.qc;

import forge.raw.exceptions;
import forge.raw.raw;

export namespace forge::chain::savanna {

struct vote_plan;

class finalizer_safety_state {
 public:
   finalizer_safety_state() = default;

   [[nodiscard]] const block_ref& last_vote() const noexcept;
   [[nodiscard]] const block_ref& lock() const noexcept;
   [[nodiscard]] block_slot_t other_branch_latest_slot() const noexcept;

 private:
   static constexpr auto raw_version = std::uint32_t{1};

   template <typename Stream>
   friend Stream& operator<<(Stream& stream,
                             const finalizer_safety_state& value) {
      forge::raw::pack(stream, raw_version);
      forge::raw::pack(stream, value.last_vote_);
      forge::raw::pack(stream, value.lock_);
      forge::raw::pack(stream, value.other_branch_latest_slot_);
      return stream;
   }

   template <typename Stream>
   friend Stream& operator>>(Stream& stream, finalizer_safety_state& value) {
      auto version = std::uint32_t{};
      auto decoded = finalizer_safety_state{};
      forge::raw::unpack(stream, version);
      if (version != raw_version) {
         FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error,
                               "unsupported Savanna finalizer safety state version",
                               forge::exceptions::ctx("version", version));
      }
      forge::raw::unpack(stream, decoded.last_vote_);
      forge::raw::unpack(stream, decoded.lock_);
      forge::raw::unpack(stream, decoded.other_branch_latest_slot_);
      try {
         decoded.require_valid();
      } catch (const exceptions::invalid_finalizer_safety_state&) {
         FORGE_THROW_EXCEPTION(forge::raw::exceptions::codec_error,
                               "Savanna finalizer safety state is corrupted");
      }
      value = std::move(decoded);
      return stream;
   }

   void require_valid() const;

   block_ref last_vote_;
   block_ref lock_;
   block_slot_t other_branch_latest_slot_ = 0;

   friend finalizer_safety_state make_finalizer_safety(block_ref);
   friend vote_plan plan_vote(const finalizer_safety_state&, const finality_core&,
                              const block_ref&);
   friend finalizer_safety_state
   advance_from_qc(finalizer_safety_state, const finality_core&,
                   const block_ref&, const verified_quorum_certificate&,
                   const forge::crypto::bls::public_key&);
};

enum class vote_decision : std::uint8_t {
   abstain,
   strong,
   weak,
};

struct vote_plan {
   vote_decision decision = vote_decision::abstain;
   bool monotonic = false;
   bool live = false;
   bool safe = false;
   finalizer_safety_state next;
};

[[nodiscard]] finalizer_safety_state
make_finalizer_safety(block_ref initial_lock);

[[nodiscard]] vote_plan
plan_vote(const finalizer_safety_state& state,
          const finality_core& candidate_core, const block_ref& candidate);

[[nodiscard]] finalizer_safety_state
advance_from_qc(finalizer_safety_state state,
                const finality_core& candidate_core,
                const block_ref& candidate,
                const verified_quorum_certificate& certificate,
                const forge::crypto::bls::public_key& finalizer);

BOOST_DESCRIBE_ENUM(vote_decision, abstain, strong, weak)

} // namespace forge::chain::savanna
