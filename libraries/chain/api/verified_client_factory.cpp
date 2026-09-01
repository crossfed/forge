module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <new>
#include <source_location>
#include <stdexcept>
#include <utility>

module forge.chain.api.verified_client_factory;

import forge.chain.api.exceptions;

namespace forge::chain::api {
namespace {

bool is_public_chain_api_failure(const forge::exceptions::base& error) {
   return error.code().category() == exceptions::forge_exceptions_category(exceptions::code::invalid_request);
}

[[noreturn]] void throw_trust_required(const forge::exceptions::base& error,
                                       std::source_location location = std::source_location::current()) {
   throw exceptions::trust_required{error.message(), error.context(), location};
}

std::optional<protocol::chain_id> preflight_trusted_chain(finality_verifier& verifier) {
   try {
      return verifier.trusted_chain();
   } catch (const std::bad_alloc&) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted,
                            "verified chain API finality chain preflight allocation failed");
   } catch (const std::length_error& error) {
      FORGE_THROW_EXCEPTION(exceptions::resource_exhausted,
                            "verified chain API finality chain preflight allocation exceeds its limit",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (const forge::exceptions::base& error) {
      if (is_public_chain_api_failure(error)) {
         throw;
      }
      throw_trust_required(error);
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "verified chain API finality chain preflight failed",
                            forge::exceptions::ctx("reason", error.what()));
   } catch (...) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required,
                            "verified chain API finality chain preflight failed with a non-standard error");
   }
}

} // namespace

verified_client make_verified_client(raw_client client, verified_client_options options) {
   if (!options.projections) {
      FORGE_THROW_EXCEPTION(exceptions::audit_not_supported,
                            "verified chain API client requires a projection verifier");
   }
   if (!options.finality) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required, "verified chain API client requires a finality verifier");
   }
   const auto finality_chain = preflight_trusted_chain(*options.finality);
   if (!finality_chain) {
      FORGE_THROW_EXCEPTION(exceptions::trust_required,
                            "verified chain API client finality verifier does not declare its trusted chain");
   }
   if (*finality_chain != options.chain) {
      FORGE_THROW_EXCEPTION(exceptions::wrong_chain,
                            "verified chain API client finality verifier belongs to another chain");
   }
   auto verifier = std::make_shared<authenticated_audit_verifier>(
       authenticated_audit_options{
           .chain = std::move(options.chain),
           .state_domain = std::move(options.state_domain),
           .proof_limits = options.proof_limits,
       },
       std::move(options.finality));
   return verified_client{std::move(client), std::move(verifier), std::move(options.projections),
                          options.service_limits};
}

} // namespace forge::chain::api
