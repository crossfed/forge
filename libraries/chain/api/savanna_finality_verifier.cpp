module;

#include <forge/exceptions/macros.hpp>

#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <span>
#include <utility>
#include <variant>

module forge.chain.api.savanna_finality_verifier;

import forge.chain.api.exceptions;
import forge.crypto.digest.sha256;

namespace forge::chain::api {
namespace {

template <typename Exception>
[[noreturn]] void throw_public(const forge::exceptions::base& error,
                               std::source_location location = std::source_location::current()) {
   throw Exception{error.message(), error.context(), location};
}

template <typename Operation> decltype(auto) translate_finality_failure(Operation&& operation) {
   try {
      return std::invoke(std::forward<Operation>(operation));
   } catch (const exceptions::trust_required&) {
      throw;
   } catch (const exceptions::wrong_chain&) {
      throw;
   } catch (const exceptions::invalid_finality&) {
      throw;
   } catch (const savanna::exceptions::finality_witness_limit_exceeded& error) {
      throw_public<exceptions::resource_exhausted>(error);
   } catch (const savanna::exceptions::finality_witness_wrong_chain& error) {
      throw_public<exceptions::wrong_chain>(error);
   } catch (const savanna::exceptions::untrusted_finality_bootstrap& error) {
      throw_public<exceptions::trust_required>(error);
   } catch (const savanna::exceptions::invalid_finality_witness& error) {
      throw_public<exceptions::invalid_finality>(error);
   } catch (const savanna::exceptions::finality_anchor_mismatch& error) {
      throw_public<exceptions::invalid_finality>(error);
   } catch (const forge::exceptions::base& error) {
      throw_public<exceptions::invalid_finality>(error);
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality, "Savanna finality witness verification failed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_finality,
                            "Savanna finality witness verification failed with a non-standard error");
   }
}

void require_witness_chain(const protocol::chain_id& expected, const savanna::finality_witness& witness) {
   if (witness.chain != expected) {
      FORGE_THROW_EXCEPTION(exceptions::wrong_chain, "Savanna finality witness belongs to another chain");
   }
}

class savanna_finality_verifier final : public finality_verifier {
 public:
   savanna_finality_verifier(savanna::finality_trust trust, savanna::finality_witness_limits limits)
       : anchor_{savanna::trust_anchor(trust)}, trust_{std::move(trust)}, limits_{limits} {}

   [[nodiscard]] std::optional<protocol::block_id> preferred_trust_anchor() const override {
      return anchor_.block;
   }

   void verify(const protocol::state_anchor& anchor, const protocol::proof_blob& proof) override {
      const auto proof_digest = forge::crypto::digest::sha256::hash(proof);
      {
         const auto lock = std::lock_guard{verified_mutex_};
         if (verified_ && verified_->anchor == anchor && verified_->proof_digest == proof_digest) {
            return;
         }
      }

      static_cast<void>(replay_state(anchor, proof));

      const auto lock = std::lock_guard{verified_mutex_};
      verified_ = verified_finality{.anchor = anchor, .proof_digest = proof_digest};
   }

   void verify_ancestry(const protocol::state_anchor& finalized, std::span<const protocol::state_anchor> intermediate,
                        const protocol::proof_blob& proof) override {
      translate_finality_failure([&] {
         const auto witness = savanna::decode_finality_witness(proof, limits_);
         require_witness_chain(finalized.chain, witness);
         savanna::verify_finality_ancestry_witness(trust_, proof, finalized, intermediate, limits_);
      });
   }

 private:
   struct verified_finality {
      protocol::state_anchor anchor;
      protocol::digest proof_digest;
   };

   [[nodiscard]] savanna::header_state replay_state(const protocol::state_anchor& anchor,
                                                    const protocol::proof_blob& proof) const {
      return translate_finality_failure([&] {
         const auto witness = savanna::decode_finality_witness(proof, limits_);
         require_witness_chain(anchor.chain, witness);
         if (witness.trusted_bootstrap != anchor_.block) {
            FORGE_THROW_EXCEPTION(exceptions::trust_required,
                                  "Savanna finality witness does not match the configured trusted bootstrap");
         }
         return savanna::replay_finality_witness_state(trust_, witness, anchor, limits_);
      });
   }

   savanna::finality_trust_anchor anchor_;
   savanna::finality_trust trust_;
   savanna::finality_witness_limits limits_;
   std::mutex verified_mutex_;
   std::optional<verified_finality> verified_;
};

} // namespace

std::shared_ptr<finality_verifier> make_savanna_finality_verifier(savanna::finality_trust trust,
                                                                  savanna::finality_witness_limits limits) {
   try {
      return std::make_shared<savanna_finality_verifier>(std::move(trust), limits);
   } catch (const exceptions::trust_required&) {
      throw;
   } catch (const forge::exceptions::base& error) {
      throw_public<exceptions::trust_required>(error);
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "Savanna finality trust is invalid",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "Savanna finality trust is invalid");
   }
}

} // namespace forge::chain::api
