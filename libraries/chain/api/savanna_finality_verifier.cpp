module;

#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

module forge.chain.api.savanna_finality_verifier;

import forge.chain.api.exceptions;
import forge.crypto.digest.sha256;

namespace forge::chain::api {
namespace {

protocol::block_num operational_block_num(const savanna::finality_trust& trust) {
   const auto* checkpoint = std::get_if<savanna::finality_checkpoint_bootstrap>(&trust);
   return checkpoint ? checkpoint->value.finalized.num : protocol::block_num{1U};
}

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
   } catch (const exceptions::invalid_request&) {
      throw;
   } catch (const exceptions::wrong_chain&) {
      throw;
   } catch (const exceptions::resource_exhausted&) {
      throw;
   } catch (const exceptions::invalid_finality&) {
      throw;
   } catch (const std::bad_alloc&) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "Savanna finality witness allocation failed");
   } catch (const std::length_error& error) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "Savanna finality witness allocation exceeds its limit",
                            forge::exceptions::ctx("reason", error.what()));
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

class trusted_finality_set {
 public:
   trusted_finality_set(savanna::finality_trust trust, std::vector<savanna::finality_trust> additional_trusts) {
      reserve(additional_trusts.size() + 1U);
      add(std::move(trust));
      for (auto& additional : additional_trusts) {
         add(std::move(additional));
      }
   }

   explicit trusted_finality_set(std::span<const savanna::finality_trust> trusted) {
      reserve(trusted.size());
      for (const auto& trust : trusted) {
         add(trust);
      }
   }

   [[nodiscard]] std::optional<protocol::block_id> preferred_trust_anchor() const {
      const auto preferred = std::ranges::max_element(trusted_, {}, &trusted_entry::operational_block_num);
      return preferred->anchor.block;
   }

   [[nodiscard]] savanna::header_state replay_state(const protocol::state_anchor& expected,
                                                    const protocol::proof_blob& proof,
                                                    savanna::finality_witness_limits limits) const {
      const auto witness = savanna::decode_finality_witness(proof, limits);
      require_witness_chain(expected.chain, witness);
      return savanna::replay_finality_witness_state(require_trust(witness), witness, expected, limits);
   }

   void verify_ancestry(const protocol::state_anchor& finalized,
                        std::span<const protocol::state_anchor> intermediate, const protocol::proof_blob& proof,
                        savanna::finality_witness_limits limits) const {
      const auto witness = savanna::decode_finality_witness(proof, limits);
      require_witness_chain(finalized.chain, witness);
      savanna::verify_finality_ancestry_witness(require_trust(witness), proof, finalized, intermediate, limits);
   }

 private:
   struct trusted_entry {
      protocol::block_num operational_block_num = 0;
      savanna::finality_trust_anchor anchor;
      savanna::finality_trust trust;
   };

   void reserve(std::size_t size) {
      if (size == 0U) {
         FORGE_THROW_EXCEPTION(exceptions::trust_required,
                               "Savanna finality verifier requires at least one trusted bootstrap");
      }
      trusted_.reserve(size);
   }

   void add(savanna::finality_trust trust) {
      auto anchor = savanna::trust_anchor(trust);
      const auto height = operational_block_num(trust);
      if (!trusted_.empty() && trusted_.front().anchor.chain != anchor.chain) {
         FORGE_THROW_EXCEPTION(exceptions::wrong_chain,
                               "Savanna finality verifier requires trusted bootstraps from one chain");
      }
      if (std::ranges::any_of(trusted_, [&](const auto& existing) { return existing.anchor == anchor; })) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_request,
                               "Savanna finality verifier contains a duplicate trusted bootstrap");
      }
      if (std::ranges::any_of(trusted_, [&](const auto& existing) {
             return existing.operational_block_num == height && existing.anchor != anchor;
          })) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_request,
                               "Savanna finality verifier contains distinct trusted bootstraps at one block height");
      }
      trusted_.push_back({
          .operational_block_num = height,
          .anchor = std::move(anchor),
          .trust = std::move(trust),
      });
   }

   [[nodiscard]] const savanna::finality_trust& require_trust(const savanna::finality_witness& witness) const {
      const auto found = std::ranges::find_if(trusted_, [&](const auto& value) {
         return value.anchor.chain == witness.chain && value.anchor.block == witness.trusted_bootstrap;
      });
      if (found == trusted_.end()) {
         FORGE_THROW_EXCEPTION(exceptions::trust_required,
                               "Savanna finality witness does not match a trusted bootstrap");
      }
      return found->trust;
   }

   std::vector<trusted_entry> trusted_;
};

template <typename... Arguments> trusted_finality_set make_trusted_finality_set(Arguments&&... arguments) {
   try {
      return trusted_finality_set{std::forward<Arguments>(arguments)...};
   } catch (const exceptions::trust_required&) {
      throw;
   } catch (const exceptions::invalid_request&) {
      throw;
   } catch (const exceptions::wrong_chain&) {
      throw;
   } catch (const exceptions::resource_exhausted&) {
      throw;
   } catch (const std::bad_alloc&) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "Savanna finality trust allocation failed");
   } catch (const std::length_error& error) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "Savanna finality trust allocation exceeds its limit",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (const forge::exceptions::base& error) {
      throw_public<exceptions::trust_required>(error);
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "Savanna finality trust is invalid",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "Savanna finality trust is invalid");
   }
}

class savanna_finality_verifier final : public finality_verifier {
 public:
   savanna_finality_verifier(trusted_finality_set trusted, savanna::finality_witness_limits limits)
       : trusted_{std::move(trusted)}, limits_{limits} {}

   [[nodiscard]] std::optional<protocol::block_id> preferred_trust_anchor() const override {
      return trusted_.preferred_trust_anchor();
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
      translate_finality_failure([&] { trusted_.verify_ancestry(finalized, intermediate, proof, limits_); });
   }

 private:
   struct verified_finality {
      protocol::state_anchor anchor;
      protocol::digest proof_digest;
   };

   [[nodiscard]] savanna::header_state replay_state(const protocol::state_anchor& anchor,
                                                    const protocol::proof_blob& proof) const {
      return translate_finality_failure([&] { return trusted_.replay_state(anchor, proof, limits_); });
   }

   trusted_finality_set trusted_;
   savanna::finality_witness_limits limits_;
   std::mutex verified_mutex_;
   std::optional<verified_finality> verified_;
};

std::shared_ptr<finality_verifier> make_public_finality_verifier(trusted_finality_set trusted,
                                                                 savanna::finality_witness_limits limits) {
   try {
      return std::make_shared<savanna_finality_verifier>(std::move(trusted), limits);
   } catch (const exceptions::trust_required&) {
      throw;
   } catch (const exceptions::invalid_request&) {
      throw;
   } catch (const exceptions::wrong_chain&) {
      throw;
   } catch (const exceptions::resource_exhausted&) {
      throw;
   } catch (const std::bad_alloc&) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "Savanna finality verifier allocation failed");
   } catch (const std::length_error& error) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted, "Savanna finality verifier allocation exceeds its limit",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (const forge::exceptions::base& error) {
      throw_public<exceptions::trust_required>(error);
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "Savanna finality trust is invalid",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "Savanna finality trust is invalid");
   }
}

} // namespace

std::shared_ptr<finality_verifier> make_savanna_finality_verifier(savanna::finality_trust trust,
                                                                  savanna::finality_witness_limits limits) {
   return make_savanna_finality_verifier_with_trusts(std::move(trust), {}, limits);
}

std::shared_ptr<finality_verifier>
make_savanna_finality_verifier_with_trusts(savanna::finality_trust trust,
                                           std::vector<savanna::finality_trust> additional_trusts,
                                           savanna::finality_witness_limits limits) {
   return make_public_finality_verifier(make_trusted_finality_set(std::move(trust), std::move(additional_trusts)),
                                        limits);
}

savanna::header_state replay_savanna_finality_state(std::span<const savanna::finality_trust> trusted,
                                                     const protocol::state_anchor& expected,
                                                     const protocol::proof_blob& proof,
                                                     savanna::finality_witness_limits limits) {
   auto trusted_set = make_trusted_finality_set(trusted);
   return translate_finality_failure([&] { return trusted_set.replay_state(expected, proof, limits); });
}

} // namespace forge::chain::api
