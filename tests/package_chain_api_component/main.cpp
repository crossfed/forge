#include <concepts>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

import package.chain_api_component.read_e2e;
import package.chain_api_component.test_support;
import package.chain_api_component.verifier_fixture;
import package.chain_api_component.write_e2e;
import forge.chain.api.admin;
import forge.chain.api.authenticated_audit_verifier;
import forge.chain.api.block;
import forge.chain.api.finality;
import forge.chain.api.info;
import forge.chain.api.state;
import forge.chain.api.submission;
import forge.chain.api.submission_client;
import forge.chain.api.table_key;
import forge.chain.api.transaction;
import forge.chain.protocol.account_authority;
import forge.chain.protocol.audit;
import forge.chain.protocol.block_query;
import forge.chain.protocol.currency_stats;
import forge.chain.protocol.full_account;
import forge.chain.protocol.generated_transaction;
import forge.chain.protocol.state_query;
import forge.chain.protocol.table;
import forge.chain.protocol.transaction_query;

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

int main() {
   static_assert(std::is_abstract_v<chain_api::info>);
   static_assert(std::is_abstract_v<chain_api::block>);
   static_assert(std::is_abstract_v<chain_api::state>);
   static_assert(std::is_abstract_v<chain_api::transaction>);
   static_assert(std::is_abstract_v<chain_api::submission>);
   static_assert(std::is_abstract_v<chain_api::admin>);
   static_assert(!std::is_abstract_v<chain_api::submission_client>);
   static_assert(std::derived_from<chain_api::authenticated_audit_verifier, chain_api::audit_verifier>);
   static_assert(std::is_abstract_v<chain_api::finality_verifier>);
   static_assert(std::is_same_v<decltype(protocol::table_rows_response{}.rows), std::vector<protocol::table_row>>);
   static_assert(std::is_same_v<decltype(protocol::table_rows_response{}.next), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::table_scope_request{}.cursor), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::table_scope_response{}.next), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::table_changes_request{}.cursor), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::account_changes_request{}.cursor), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::authorizers_request{}.cursor), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::authorizers_response{}.next), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::table_mutation{}.table), protocol::table_change_selector>);
   static_assert(
       std::is_same_v<decltype(protocol::table_changes_response{}.blocks), std::vector<protocol::table_change_batch>>);
   static_assert(std::is_same_v<decltype(protocol::account_changes_response{}.blocks),
                                std::vector<protocol::account_change_batch>>);
   static_assert(std::is_same_v<decltype(protocol::account_response{}.account), protocol::full_account>);
   static_assert(
       std::is_same_v<decltype(protocol::account_mutation{}.authority), std::optional<protocol::account_authority>>);
   static_assert(std::is_same_v<decltype(protocol::table_scope_response{}.tables), std::vector<protocol::table>>);
   static_assert(std::is_same_v<decltype(protocol::currency_stats_response{}.stats), protocol::currency_stats>);
   static_assert(std::is_same_v<decltype(protocol::scheduled_response{}.transactions),
                                std::vector<protocol::generated_transaction>>);
   package_chain_api_component::require(chain_api::state::ref().major == 3U,
                                        "installed state API does not expose version 3");

   package_chain_api_component::run_verifier_component_checks();

   auto request = protocol::table_changes_request{
       .from_block = 39,
       .to_block = 40,
       .tables = {{.code = protocol::account_name{"tester"}}},
   };
   auto block = protocol::block_request{};
   auto transaction = protocol::transaction_status_request{};
   const auto table_key = chain_api::encode_table_key(std::uint64_t{42U});
   (void)request;
   (void)block;
   (void)transaction;
   package_chain_api_component::require(table_key.size() == sizeof(std::uint64_t),
                                        "installed table key codec returned the wrong width");

   package_chain_api_component::run_read_e2e();
   package_chain_api_component::run_write_e2e();
   return 0;
}
