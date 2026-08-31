module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <utility>

module forge.chain.api.verified_client_factory;

import forge.chain.api.exceptions;

namespace forge::chain::api {

verified_client make_verified_client(raw_client client, verified_client_options options) {
   if (!options.projections) {
      FORGE_THROW_EXCEPTION(exceptions::audit_not_supported,
                            "verified chain API client requires a projection verifier");
   }

   auto finality = make_savanna_finality_verifier(std::move(options.trust));
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
