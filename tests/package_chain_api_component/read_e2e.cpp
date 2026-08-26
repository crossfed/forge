module;

#include <memory>
#include <vector>

module package.chain_api_component.read_e2e;

import package.chain_api_component.read_fixture;
import package.chain_api_component.read_e2e_http;
import package.chain_api_component.read_e2e_p2p_info_block;
import package.chain_api_component.read_e2e_p2p_state;
import package.chain_api_component.read_result;
import forge.raw.raw;

namespace package_chain_api_component {

void run_read_e2e() {
   const auto expectations = make_read_expectations();
   auto services = make_read_services(expectations);
   const auto http = run_http_read_e2e(services);
   auto p2p_info_block_state = std::make_shared<read_p2p_info_block_fixture>();
   p2p_info_block_state->information = expectations.information;
   p2p_info_block_state->block = expectations.block;
   const auto p2p_info_block = run_p2p_info_block_e2e(p2p_info_block_state);
   auto p2p_state_state = std::make_shared<read_p2p_state_fixture>();
   p2p_state_state->response = expectations.state;
   const auto p2p_state = run_p2p_state_e2e(p2p_state_state);
   const auto p2p = read_responses{
       .information = p2p_info_block.information,
       .block = p2p_info_block.block,
       .state = p2p_state.state,
       .oversized_request_rejected = p2p_state.oversized_request_rejected,
   };

   require(http.information == expectations.information, "HTTP info API changed chain audit DTO semantics");
   require(http.information.head_time == expectations.information.head_time,
           "HTTP info API lost head time microseconds");
   require(http.information.finalized_time == expectations.information.finalized_time,
           "HTTP info API lost finalized time microseconds");
   require(http.block == expectations.block, "HTTP block API changed typed DTO semantics");
   require(http.state == expectations.state, "HTTP state API changed typed DTO semantics");
   require(http.oversized_request_rejected, "HTTP Chain API request limit was not exercised");
   require(p2p.information == expectations.information, "P2P info API changed chain audit DTO semantics");
   require(p2p.block == expectations.block, "P2P block API changed typed DTO semantics");
   require(p2p.state == expectations.state, "P2P state API changed chain audit DTO semantics");
   require(p2p.oversized_request_rejected, "P2P Chain API request limit was not exercised");
   require(http.information == p2p.information, "HTTP and P2P info API responses diverged");
   require(http.block == p2p.block, "HTTP and P2P block API responses diverged");
   require(http.state == p2p.state, "HTTP and P2P state API responses diverged");
   require(forge::raw::pack(http.information) == forge::raw::pack(p2p.information),
           "HTTP and P2P info canonical bytes diverged");
   require(forge::raw::pack(http.block) == forge::raw::pack(p2p.block), "HTTP and P2P block canonical bytes diverged");
   require(forge::raw::pack(http.state) == forge::raw::pack(p2p.state), "HTTP and P2P state canonical bytes diverged");
   require_audit_semantics(http.information);
   require_audit_semantics(http.block);
   require_audit_semantics(http.state, 1U);
   require_audit_semantics(p2p.information);
   require_audit_semantics(p2p.block);
   require_audit_semantics(p2p.state, 1U);
   require(services.information_calls() + p2p_info_block_state->information_calls.load() == 2,
           "info transport E2E did not dispatch both typed calls");
   require(services.block_calls() + p2p_info_block_state->block_calls.load() == 2,
           "transport E2E did not dispatch both block typed calls");
   require(services.state_calls() + p2p_state_state->calls.load() == 2,
           "state transport E2E did not dispatch both typed calls");
   require(services.information_audit_required(), "info transport E2E changed the requested audit mode");
   require(p2p_info_block_state->information_audit.load() == forge::chain::protocol::audit_mode::required,
           "P2P info API changed the requested audit mode");
   require(services.block_audit_required() &&
               p2p_info_block_state->block_audit.load() == forge::chain::protocol::audit_mode::required,
           "P2P block API changed the requested audit mode");
   require(services.state_audit_required() &&
               p2p_state_state->audit.load() == forge::chain::protocol::audit_mode::required,
           "P2P state API changed the requested audit mode");
}

} // namespace package_chain_api_component
