module;

#include <memory>
#include <span>

module package.chain_api_component.verifier_fixture;

import package.chain_api_component.test_support;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.chain.api.authenticated_audit_verifier;
import forge.chain.api.exceptions;
import forge.chain.api.finality;
import forge.chain.api.raw_client;
import forge.chain.api.verified_client;
import forge.chain.protocol.audit;
import forge.chain.protocol.block_query;

namespace package_chain_api_component {

void accepting_finality::verify(const forge::chain::protocol::state_anchor&, const forge::chain::protocol::proof_blob&) {
   ++calls;
}

void accepting_finality::verify_ancestry(const forge::chain::protocol::state_anchor&,
                                         std::span<const forge::chain::protocol::state_anchor>,
                                         const forge::chain::protocol::proof_blob&) {
   ++calls;
}

void run_verifier_component_checks() {
   namespace chain_api = forge::chain::api;
   namespace protocol = forge::chain::protocol;

   const auto finality_delegate = std::make_shared<accepting_finality>();
   const auto finality = std::make_shared<chain_api::cached_finality_verifier>(finality_delegate, 4U);
   const auto anchor = protocol::state_anchor{
       .chain = hash("package-verifier-chain"),
       .block = hash("package-verifier-anchor"),
   };
   finality->verify(anchor, {});
   require(finality_delegate->calls == 1U, "installed cached finality verifier did not invoke its delegate");
   const auto audit = std::make_shared<chain_api::authenticated_audit_verifier>(
       chain_api::authenticated_audit_options{.chain = anchor.chain, .state_domain = "package-test"}, finality);
   audit->verify_context(protocol::response_context{.chain = anchor.chain});
   auto audit_anchor = anchor;
   audit_anchor.block = hash("package-audit-anchor");
   audit->verify_finality(audit_anchor, {});
   require(finality_delegate->calls == 2U, "installed authenticated verifier did not invoke finality verification");
   const auto projections = std::make_shared<chain_api::projection_verifier>();
   auto projection_rejected = false;
   try {
      projections->verify(protocol::block_request{}, protocol::block_state_response{}, protocol::audit_bundle{}, *audit);
   } catch (const chain_api::exceptions::audit_not_supported&) {
      projection_rejected = true;
   }
   require(projection_rejected, "installed default projection verifier did not fail closed");
   auto verified = chain_api::verified_client{chain_api::raw_client{chain_api::service_handles{}}, audit, projections};
   auto verifier_runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 1}};
   auto verified_rejected = false;
   try {
      static_cast<void>(forge::asio::blocking::run(verifier_runtime, verified.get_info()));
   } catch (const chain_api::exceptions::unavailable&) {
      verified_rejected = true;
   }
   require(verified_rejected, "installed verified client did not reject a missing transport");
}

} // namespace package_chain_api_component
