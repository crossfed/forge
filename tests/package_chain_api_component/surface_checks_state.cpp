module;

#include <optional>
#include <type_traits>
#include <vector>

module package.chain_api_component.surface_checks;

import package.chain_api_component.test_support;
import forge.chain.api.state;
import forge.codec.json;
import forge.variant.described;
import forge.variant.value;

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

   auto account = protocol::full_account{};
   auto permission = protocol::full_permission{};
   permission.auth.threshold = 1;
   account.permissions.push_back(permission);

   auto variant = forge::variant{};
   forge::to_variant(account, variant);
   auto variant_round_trip = protocol::full_account{};
   forge::from_variant(variant, variant_round_trip);
   require(variant_round_trip == account, "state API did not preserve full-account authority through Variant");

   const auto http_json = forge::codec::json::write(account);
   require(http_json.ok(), "state API could not encode full-account authority as HTTP JSON");
   const auto http_round_trip = forge::codec::json::read<protocol::full_account>(
       http_json.text, {.source_name = "package.state.full-account",
                        .unknown_fields = forge::codec::json::unknown_field_policy::error,
                        .described_records = forge::codec::json::described_record_policy::exact});
   require(http_round_trip.ok(), "state API could not decode full-account authority from HTTP JSON");
   require(http_round_trip.value == account, "state API changed full-account authority through HTTP JSON");
}

} // namespace package_chain_api_component
