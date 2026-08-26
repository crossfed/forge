module;

#include <memory>
#include <vector>

module package.chain_api_component.write_e2e;

import package.chain_api_component.write_fixture;
import package.chain_api_component.write_e2e_http;
import package.chain_api_component.write_e2e_p2p_admin;
import package.chain_api_component.write_e2e_p2p_transaction;
import package.chain_api_component.write_result;
import forge.raw.raw;

namespace package_chain_api_component {

void run_write_e2e() {
   const auto expectations = make_write_expectations();
   auto services = make_write_services(expectations);
   const auto http = run_http_write_e2e(services);
   auto p2p_transaction_state = std::make_shared<write_p2p_transaction_fixture>();
   p2p_transaction_state->response = expectations.transaction;
   const auto p2p_transaction = run_p2p_transaction_e2e(p2p_transaction_state);
   auto p2p_admin_state = std::make_shared<write_p2p_admin_fixture>();
   p2p_admin_state->response = expectations.administration;
   const auto p2p_admin = run_p2p_admin_e2e(p2p_admin_state);
   const auto p2p = write_responses{
       .transaction = p2p_transaction.transaction,
       .administration = p2p_admin.administration,
       .internal_error_preserved = p2p_admin.internal_error_preserved,
   };
   require(http.transaction == expectations.transaction, "HTTP transaction API changed typed DTO semantics");
   require(http.administration == expectations.administration, "HTTP admin API changed typed DTO semantics");
   require(p2p.transaction == expectations.transaction, "P2P transaction API changed typed DTO semantics");
   require(p2p.administration == expectations.administration, "P2P admin API changed typed DTO semantics");
   require(p2p.internal_error_preserved, "P2P admin error semantics were not exercised");
   require(http.transaction == p2p.transaction, "HTTP and P2P transaction API responses diverged");
   require(http.administration == p2p.administration, "HTTP and P2P admin API responses diverged");
   require(forge::raw::pack(http.transaction) == forge::raw::pack(p2p.transaction),
           "HTTP and P2P transaction canonical bytes diverged");
   require(forge::raw::pack(http.administration) == forge::raw::pack(p2p.administration),
           "HTTP and P2P admin canonical bytes diverged");
   require_audit_semantics(http.transaction);
   require_audit_semantics(p2p.transaction);
   require(services.transaction_calls() + p2p_transaction_state->calls.load() == 2,
           "transport E2E did not dispatch both transaction typed calls");
   require(services.administration_calls() + p2p_admin_state->calls.load() == 2,
           "transport E2E did not dispatch both admin typed calls");
   require(p2p_admin_state->error_calls.load() == 1, "P2P admin API did not dispatch its typed error call");
   require(services.transaction_audit_required() &&
               p2p_transaction_state->audit.load() == forge::chain::protocol::audit_mode::required,
           "P2P transaction API changed the requested audit mode");
}

} // namespace package_chain_api_component
