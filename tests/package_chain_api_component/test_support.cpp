module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

module package.chain_api_component.test_support;

import forge.chain.protocol.audit;
import forge.chain.protocol.info;
import forge.crypto.digest.sha256;

namespace package_chain_api_component {

namespace protocol = forge::chain::protocol;

void require(bool condition, std::string_view message) {
   if (!condition) {
      throw std::runtime_error{std::string{message}};
   }
}

protocol::digest hash(std::string_view value) {
   return forge::crypto::digest::sha256::hash(std::string{value});
}

protocol::service_limits package_limits() {
   return {
       .max_page_size = 256,
       .max_state_batch_size = 32,
       .max_transaction_batch_size = 16,
       .max_container_elements = 1'024,
       .max_transaction_status_candidates = 512,
       .max_request_bytes = chain_api_max_request_size,
       .max_response_bytes = chain_api_max_request_size,
       .max_proof_bytes = 1U << 20U,
       .max_await_ms = 300'000,
       .state_retention_blocks = 512,
   };
}

protocol::info_response make_audited_info_response() {
   const auto chain = hash("chain-api-e2e-chain");
   const auto head = hash("chain-api-e2e-head");
   const auto finalized = hash("chain-api-e2e-finalized");

   auto response = protocol::info_response{};
   response.context = protocol::response_context{
       .chain = chain,
       .head = head,
       .finalized = finalized,
       .anchor = protocol::state_anchor{
           .chain = chain,
           .block = finalized,
           .block_num = 40,
           .transaction_root = hash("chain-api-e2e-transactions"),
           .state_root = hash("chain-api-e2e-state"),
           .state_size = 17,
           .change_root = hash("chain-api-e2e-changes"),
           .change_count = 5,
       },
   };
   response.audit = protocol::audit_bundle{
       .finality = protocol::proof_blob{
           .scheme = "forge.chain.finality.test",
           .version = 3,
           .payload = {0x01, 0x02, 0x03},
       },
       .state = {
           protocol::proof_blob{
               .scheme = "forge.db.authenticated.point",
               .version = 2,
               .payload = {0x10, 0x11, 0x12, 0x13},
           },
           protocol::proof_blob{
               .scheme = "forge.db.authenticated.range",
               .version = 4,
               .payload = {0x20, 0x21, 0x22},
           },
       },
       .transaction = protocol::transaction_inclusion_proof{
           .leaf = hash("chain-api-e2e-transaction"),
           .index = 3,
           .leaf_count = 8,
           .path = {
               protocol::merkle_step{.sibling = hash("chain-api-e2e-sibling-left"), .sibling_on_left = true},
               protocol::merkle_step{.sibling = hash("chain-api-e2e-sibling-right"), .sibling_on_left = false},
           },
       },
   };
   response.chain = chain;
   response.server_version = "1.2.3";
   response.server_version_string = "forge-chain-api-e2e";
   response.server_full_version_string = "forge-chain-api-e2e+transport";
   response.head = head;
   response.head_num = 42;
   response.head_time = protocol::time_point{protocol::microseconds{1'700'000'000'123'456LL}};
   response.finalized = finalized;
   response.finalized_num = 40;
   response.finalized_time = protocol::time_point{protocol::microseconds{1'699'999'999'654'321LL}};
   response.best_candidate = hash("chain-api-e2e-candidate");
   response.best_candidate_num = 43;
   response.earliest_available_block_num = 7;
   response.virtual_block_cpu_limit = 1'000;
   response.virtual_block_net_limit = 2'000;
   response.block_cpu_limit = 900;
   response.block_net_limit = 1'800;
   response.total_cpu_weight = 77;
   response.total_net_weight = 88;
   response.available.archive = true;
   response.limits = package_limits();
   return response;
}

void require_audit_semantics(const protocol::audited_response& response, std::size_t state_proofs) {
   require(response.context.anchor.has_value(), "transport dropped the audit anchor");
   require(response.audit.has_value(), "transport dropped the audit bundle");
   require(response.audit->finality.has_value(), "transport dropped the finality proof");
   require(std::ranges::equal(response.audit->finality->payload,
                              std::array<std::uint8_t, 3>{0x01, 0x02, 0x03}),
           "transport changed finality proof bytes");
   require(response.audit->state.size() == state_proofs, "transport changed state proof cardinality");
   if (state_proofs == 1U) {
      require(response.audit->state.front().scheme == "forge.db.authenticated.changes",
              "transport changed typed change proof metadata");
   } else {
      require(response.audit->state[1].scheme == "forge.db.authenticated.range",
              "transport changed ranked range proof metadata");
   }
   require(response.audit->transaction.has_value(), "transport dropped transaction inclusion proof");
   require(response.audit->transaction->path.size() == 2, "transport changed transaction proof path");
}

} // namespace package_chain_api_component
