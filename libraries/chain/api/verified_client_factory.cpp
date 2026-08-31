module;

#include <forge/exceptions/macros.hpp>

#include <exception>
#include <memory>
#include <utility>

module forge.chain.api.verified_client_factory;

import forge.chain.api.exceptions;

namespace forge::chain::api {
namespace {

void require_trusted_chain(const protocol::chain_id& expected, const savanna::finality_trust& trust) {
   try {
      if (savanna::trust_anchor(trust).chain != expected) {
         FORGE_THROW_EXCEPTION(exceptions::wrong_chain, "verified chain API client trust belongs to another chain");
      }
   } catch (const exceptions::wrong_chain&) {
      throw;
   } catch (const forge::exceptions::base& error) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "Savanna finality trust is invalid",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "Savanna finality trust is invalid",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "Savanna finality trust is invalid");
   }
}

} // namespace

verified_client make_verified_client(raw_client client, verified_client_options options) {
   if (!options.projections) {
      FORGE_THROW_EXCEPTION(exceptions::audit_not_supported,
                            "verified chain API client requires a projection verifier");
   }

   require_trusted_chain(options.chain, options.trust);
   auto finality = make_savanna_finality_verifier_with_trusts(std::move(options.trust),
                                                               std::move(options.additional_trusts));
   auto verifier = std::make_shared<authenticated_audit_verifier>(
       authenticated_audit_options{
           .chain = std::move(options.chain),
           .state_domain = std::move(options.state_domain),
           .proof_limits = options.proof_limits,
       },
       std::move(finality));
   return verified_client{std::move(client), std::move(verifier), std::move(options.projections),
                          options.service_limits};
}

} // namespace forge::chain::api
