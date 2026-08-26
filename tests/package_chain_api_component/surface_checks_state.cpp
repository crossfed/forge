module;

#include <optional>
#include <type_traits>
#include <vector>

module package.chain_api_component.surface_checks;

import forge.chain.protocol.account_authority;
import forge.chain.protocol.currency_stats;
import forge.chain.protocol.full_account;
import forge.chain.protocol.generated_transaction;
import forge.chain.protocol.state_query;
import forge.chain.protocol.table;

namespace package_chain_api_component {

void check_state_surface() {
   namespace protocol = forge::chain::protocol;

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
}

} // namespace package_chain_api_component
