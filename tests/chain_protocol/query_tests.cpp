#include <boost/test/unit_test.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import forge.chain.protocol.account_authority;
import forge.chain.protocol.admin;
import forge.chain.protocol.activated_protocol_feature;
import forge.chain.protocol.block_query;
import forge.chain.protocol.full_account;
import forge.chain.protocol.info;
import forge.chain.protocol.producer_info;
import forge.chain.protocol.state_query;
import forge.chain.protocol.table;
import forge.chain.protocol.transaction_query;
import forge.codec.json;
import forge.crypto.digest.sha256;
import forge.raw.exceptions;
import forge.raw.raw;

namespace protocol = forge::chain::protocol;

namespace {

template <typename... Values> protocol::bytes concatenate_raw(const Values&... values) {
   auto result = protocol::bytes{};
   const auto append = [&result](const auto& value) {
      const auto bytes = forge::raw::pack(value);
      result.insert(result.end(), bytes.begin(), bytes.end());
   };
   (append(values), ...);
   return result;
}

} // namespace

BOOST_AUTO_TEST_CASE(transaction_submission_deadlines_are_canonical_raw_fields) {
   auto request = protocol::transaction_submit_request{.retry = true, .retry_blocks = 7U, .timeout_ms = 12'345U};
   const auto encoded = forge::raw::pack(request);
   const auto timeout = forge::raw::pack(request.timeout_ms);
   BOOST_REQUIRE(encoded.size() >= timeout.size());
   BOOST_TEST((protocol::bytes{encoded.end() - static_cast<std::ptrdiff_t>(timeout.size()), encoded.end()} == timeout));

   const auto decoded = forge::raw::unpack_exact<protocol::transaction_submit_request>(encoded);
   BOOST_TEST(decoded.retry);
   BOOST_REQUIRE(decoded.retry_blocks.has_value());
   BOOST_TEST(*decoded.retry_blocks == 7U);
   BOOST_TEST(decoded.timeout_ms == 12'345U);

   auto legacy = encoded;
   legacy.resize(legacy.size() - timeout.size());
   BOOST_CHECK_THROW((void)forge::raw::unpack_exact<protocol::transaction_submit_request>(legacy),
                     forge::raw::exceptions::range_error);

   const auto batch = protocol::transaction_submit_batch_request{
       .transactions = {request},
       .timeout_ms = 20'000U,
   };
   const auto batch_decoded =
       forge::raw::unpack_exact<protocol::transaction_submit_batch_request>(forge::raw::pack(batch));
   BOOST_REQUIRE(batch_decoded.transactions.size() == 1U);
   BOOST_TEST(batch_decoded.transactions.front().timeout_ms == request.timeout_ms);
   BOOST_TEST(batch_decoded.timeout_ms == batch.timeout_ms);

   const auto json = forge::codec::json::write(batch);
   BOOST_REQUIRE(json.ok());
   const auto json_value = forge::codec::json::read_value(json.text);
   BOOST_REQUIRE(json_value.ok());
   BOOST_TEST(json_value.value["timeout_ms"].as_uint64() == batch.timeout_ms);
   BOOST_TEST(json_value.value["transactions"][std::size_t{0}]["timeout_ms"].as_uint64() == request.timeout_ms);
}

BOOST_AUTO_TEST_CASE(table_rows_roundtrip_canonical_binary_contract) {
   const auto request = protocol::table_rows_request{
       .code = protocol::account_name{"eosio.token"},
       .scope = protocol::name{"alice"},
       .table = protocol::name{"accounts"},
       .index = {.kind = protocol::table_index_kind::secondary_u128, .position = 2U},
       .lower_bound = protocol::bytes{0x00U, 0x01U},
       .upper_bound = protocol::bytes{0xfeU, 0xffU},
       .cursor = protocol::bytes{0x10U, 0x20U},
       .limit = 25U,
       .reverse = true,
       .audit = protocol::audit_mode::required,
   };
   const auto encoded = forge::codec::json::write(request);
   BOOST_REQUIRE(encoded.ok());
   const auto decoded = forge::codec::json::read<protocol::table_rows_request>(
       encoded.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(decoded.ok());
   BOOST_CHECK(decoded.value == request);

   auto response = protocol::table_rows_response{};
   response.rows = {{.value = {0xdeU, 0xadU}, .payer = protocol::account_name{"eosio.token"}}};
   response.next = protocol::bytes{0x30U, 0x40U};
   const auto response_json = forge::codec::json::write(response);
   BOOST_REQUIRE(response_json.ok());
   const auto response_value = forge::codec::json::read_value(response_json.text);
   BOOST_REQUIRE(response_value.ok());
   BOOST_TEST(response_value.value["rows"][std::size_t{0}]["value"][std::size_t{0}].as_uint64() == 0xdeU);
   BOOST_TEST(response_value.value["next"][std::size_t{1}].as_uint64() == 0x40U);
}

BOOST_AUTO_TEST_CASE(table_scope_pagination_roundtrips_opaque_bytes_in_exact_json) {
   auto request = protocol::table_scope_request{
       .code = protocol::account_name{"eosio.token"},
       .table = protocol::name{"accounts"},
       .lower_bound = "alice",
       .upper_bound = "zebra",
       .limit = 25U,
       .reverse = true,
       .cursor = protocol::bytes{0x00U, 0x2fU, 0xffU},
       .audit = protocol::audit_mode::required,
   };
   const auto request_json = forge::codec::json::write(request);
   BOOST_REQUIRE(request_json.ok());
   const auto request_value = forge::codec::json::read_value(request_json.text);
   BOOST_REQUIRE(request_value.ok());
   const auto& cursor = request_value.value["cursor"].get_array();
   BOOST_REQUIRE_EQUAL(cursor.size(), 3U);
   BOOST_TEST(cursor[0].as_uint64() == 0U);
   BOOST_TEST(cursor[1].as_uint64() == 0x2fU);
   BOOST_TEST(cursor[2].as_uint64() == 0xffU);

   const auto exact = forge::codec::json::read<protocol::table_scope_request>(
       request_json.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(exact.ok());
   BOOST_CHECK(exact.value == request);

   auto response = protocol::table_scope_response{};
   response.tables = {{
       .id = protocol::table_id{1U},
       .code = protocol::account_name{"eosio.token"},
       .scope = protocol::name{"alice"},
       .table = protocol::table_name{"accounts"},
       .payer = protocol::account_name{"eosio.token"},
       .count = 1U,
   }};
   response.next = protocol::bytes{0x01U, 0x02U, 0x03U};
   const auto response_json = forge::codec::json::write(response);
   BOOST_REQUIRE(response_json.ok());
   const auto response_value = forge::codec::json::read_value(response_json.text);
   BOOST_REQUIRE(response_value.ok());
   const auto& object = response_value.value.get_object();
   BOOST_TEST(object.contains("tables"));
   BOOST_TEST(!object.contains("rows"));
   BOOST_TEST(object.contains("next"));
   BOOST_TEST(!object.contains("more"));
   BOOST_TEST(!object.contains("next_key"));

   const auto exact_response = forge::codec::json::read<protocol::table_scope_response>(
       response_json.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(exact_response.ok());
   BOOST_CHECK(exact_response.value == response);
}

BOOST_AUTO_TEST_CASE(typed_state_changes_roundtrip_opaque_cursors_and_canonical_account_projections) {
   const auto table_request = protocol::table_changes_request{
       .from_block = 10U,
       .to_block = 12U,
       .tables =
           {
               {.code = protocol::account_name{"alpha"},
                .scope = protocol::name{"scope"},
                .table = protocol::name{"rows"}},
               {.code = protocol::account_name{"beta"},
                .scope = protocol::name{"scope"},
                .table = protocol::name{"rows"}},
           },
       .limit = 25U,
       .cursor = protocol::bytes{0x00U, 0x2fU, 0xffU},
       .audit = protocol::audit_mode::required,
   };
   BOOST_CHECK(forge::raw::unpack_exact<protocol::table_changes_request>(forge::raw::pack(table_request)) ==
               table_request);
   const auto table_json = forge::codec::json::write(table_request);
   BOOST_REQUIRE(table_json.ok());
   const auto decoded_table = forge::codec::json::read<protocol::table_changes_request>(
       table_json.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(decoded_table.ok());
   BOOST_CHECK(decoded_table.value == table_request);

   auto first_anchor = protocol::state_anchor{.block_num = 11U};
   first_anchor.block._hash[0] = 0x11U;
   auto target_anchor = protocol::state_anchor{.block_num = 12U};
   target_anchor.block._hash[0] = 0x12U;
   const auto table_response = protocol::table_changes_response{
       .blocks =
           {
               {.anchor = first_anchor,
                .mutations = {{.table = table_request.tables.front(),
                               .primary = 7U,
                               .row = protocol::table_row{.value = {0xaaU}}}}},
               {.anchor = target_anchor, .mutations = {{.table = table_request.tables.back(), .primary = 9U}}},
           },
       .next = protocol::bytes{0x01U, 0x02U},
   };
   BOOST_CHECK(forge::raw::unpack_exact<protocol::table_changes_response>(forge::raw::pack(table_response)) ==
               table_response);
   const auto table_response_json = forge::codec::json::write(table_response);
   BOOST_REQUIRE(table_response_json.ok());
   const auto table_response_value = forge::codec::json::read_value(table_response_json.text);
   BOOST_REQUIRE(table_response_value.ok());
   BOOST_TEST(table_response_value.value.get_object().contains("blocks"));
   BOOST_TEST(!table_response_value.value.get_object().contains("changes"));

   const auto account_name = protocol::account_name{"alice"};
   auto account = protocol::full_account{};
   account.name = account_name;
   auto authority = protocol::account_authority{};
   authority.name = account_name;
   const auto account_response = protocol::account_response{.account = account};
   const auto changes = protocol::account_changes_response{
       .blocks = {{.anchor = target_anchor,
                   .mutations = {{.account = account_name, .authority = authority},
                                 {.account = protocol::account_name{"bob"}}}}},
       .next = protocol::bytes{0x01U, 0x02U},
   };
   const auto decoded_account =
       forge::raw::unpack_exact<protocol::account_response>(forge::raw::pack(account_response));
   BOOST_CHECK(decoded_account.account == account);
   const auto decoded_changes = forge::raw::unpack_exact<protocol::account_changes_response>(forge::raw::pack(changes));
   BOOST_CHECK(decoded_changes == changes);
   BOOST_REQUIRE(decoded_changes.blocks.front().mutations.front().authority);
   BOOST_CHECK(decoded_changes.blocks.front().mutations.front().authority == authority);
   BOOST_TEST(!decoded_changes.blocks.front().mutations.back().authority.has_value());
}

BOOST_AUTO_TEST_CASE(authorizer_pagination_roundtrips_opaque_bytes) {
   const auto request = protocol::authorizers_request{
       .accounts = {protocol::permission_level{protocol::account_name{"alice"}, protocol::permission_name{"active"}}},
       .limit = 25U,
       .cursor = protocol::bytes{0x00U, 0x2fU, 0xffU},
       .audit = protocol::audit_mode::required,
   };
   BOOST_CHECK(forge::raw::unpack_exact<protocol::authorizers_request>(forge::raw::pack(request)) == request);

   const auto encoded = forge::codec::json::write(request);
   BOOST_REQUIRE(encoded.ok());
   const auto decoded = forge::codec::json::read<protocol::authorizers_request>(
       encoded.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(decoded.ok());
   BOOST_CHECK(decoded.value == request);

   const auto response = protocol::authorizers_response{.next = protocol::bytes{0x01U, 0x02U}};
   BOOST_CHECK(forge::raw::unpack_exact<protocol::authorizers_response>(forge::raw::pack(response)) == response);
}

BOOST_AUTO_TEST_CASE(block_info_admin_queries_reuse_canonical_typed_records) {
   static_assert(!std::derived_from<protocol::activated_protocol_feature, protocol::protocol_feature>);
   static_assert(std::derived_from<protocol::activated_protocol_feature_info, protocol::protocol_feature>);
   static_assert(std::derived_from<protocol::supported_protocol_feature, protocol::protocol_feature>);
   static_assert(std::same_as<decltype(protocol::protocol_features_response{}.features),
                              std::vector<protocol::activated_protocol_feature_info>>);
   static_assert(std::same_as<decltype(protocol::consensus_parameters_response{}.parameters), protocol::chain_config>);
   static_assert(std::same_as<decltype(protocol::consensus_parameters_response{}.wasm),
                              std::optional<protocol::wasm_parameters>>);
   static_assert(
       std::same_as<decltype(protocol::producers_request{}.lower_bound), std::optional<protocol::account_name>>);
   static_assert(std::same_as<decltype(protocol::producers_request{}.cursor), std::optional<protocol::bytes>>);
   static_assert(std::same_as<decltype(protocol::producers_response{}.rows), std::vector<protocol::producer_info>>);
   static_assert(std::same_as<decltype(protocol::producers_response{}.total_vote_weight), protocol::float64>);
   static_assert(std::same_as<decltype(protocol::producers_response{}.next), std::optional<protocol::bytes>>);
   static_assert(std::same_as<decltype(protocol::finalizer_info_response{}.last_votes),
                              std::vector<protocol::finalizer_vote_record>>);
   static_assert(std::same_as<decltype(protocol::info_response{}.resource_config), protocol::resource_limits_config>);
   static_assert(std::same_as<decltype(protocol::info_response{}.resource_state), protocol::resource_limits_state>);
   static_assert(std::same_as<decltype(protocol::ram_corrections_response{}.rows),
                              std::vector<protocol::account_ram_correction>>);

   auto activated_feature = protocol::activated_protocol_feature_info{};
   activated_feature.activation_ordinal = 41U;
   activated_feature.activation_block_num = 42U;
   activated_feature.feature_digest = protocol::digest::hash(std::string{"feature"});
   activated_feature.description_digest = protocol::digest::hash(std::string{"description"});
   activated_feature.dependencies = {protocol::digest::hash(std::string{"dependency"})};
   activated_feature.protocol_feature_type = "builtin";
   activated_feature.specification = {{.name = "builtin_feature_codename", .value = "TEST"}};
   const auto feature_response = protocol::protocol_features_response{
       .features = {activated_feature},
       .next = 43U,
   };
   BOOST_CHECK(forge::raw::unpack_exact<protocol::protocol_features_response>(forge::raw::pack(feature_response)) ==
               feature_response);

   const auto consensus = protocol::consensus_parameters_response{
       .parameters = protocol::chain_config{},
       .wasm = protocol::wasm_parameters{},
   };
   const auto decoded_consensus =
       forge::raw::unpack_exact<protocol::consensus_parameters_response>(forge::raw::pack(consensus));
   BOOST_CHECK(decoded_consensus.parameters == consensus.parameters);
   BOOST_CHECK(decoded_consensus.wasm == consensus.wasm);

   const auto producer_request = protocol::producers_request{
       .lower_bound = protocol::account_name{"alice"},
       .limit = 25U,
       .cursor = protocol::bytes{0x00U, 0x2fU, 0xffU},
       .audit = protocol::audit_mode::required,
   };
   BOOST_CHECK(forge::raw::unpack_exact<protocol::producers_request>(forge::raw::pack(producer_request)) ==
               producer_request);

   const auto producer = protocol::producer_info{.owner = protocol::account_name{"alice"}};
   auto producer_with_authority = protocol::producer_info{.owner = protocol::account_name{"bob"}};
   producer_with_authority.producer_authority =
       protocol::block_signing_authority{protocol::block_signing_authority_v0{.threshold = 1U}};
   const auto producer_response = protocol::producers_response{
       .rows = {producer, producer_with_authority},
       .total_vote_weight = protocol::float64{.bits = 0x3ff8000000000000ULL},
       .next = protocol::bytes{0x01U, 0x02U},
   };
   const auto producer_response_raw = forge::raw::pack(producer_response);
   BOOST_CHECK(forge::raw::pack(forge::raw::unpack_exact<protocol::producers_response>(producer_response_raw)) ==
               producer_response_raw);

   const auto framed_producer_response = [](const protocol::bytes& row) {
      return forge::raw::pack(protocol::audited_response{}, std::vector<protocol::bytes>{row}, protocol::float64{},
                              std::optional<protocol::bytes>{});
   };

   auto oversized_url_row = concatenate_raw(producer.owner, producer.total_votes, producer.producer_key,
                                            producer.is_active, forge::unsigned_int{128U});
   BOOST_REQUIRE(oversized_url_row.size() < 128U);
   BOOST_CHECK_THROW(
       (void)forge::raw::unpack_exact<protocol::producers_response>(framed_producer_response(oversized_url_row)),
       forge::raw::exceptions::allocation_limit);

   auto oversized_authority_keys_row = forge::raw::pack(producer_with_authority);
   BOOST_REQUIRE(!oversized_authority_keys_row.empty());
   BOOST_REQUIRE(oversized_authority_keys_row.back() == 0U);
   oversized_authority_keys_row.back() = 0x80U;
   oversized_authority_keys_row.push_back(0x01U);
   BOOST_REQUIRE(oversized_authority_keys_row.size() < 128U);
   BOOST_CHECK_THROW((void)forge::raw::unpack_exact<protocol::producers_response>(
                         framed_producer_response(oversized_authority_keys_row)),
                     forge::raw::exceptions::allocation_limit);

   const auto finalizers = protocol::finalizer_info_response{
       .last_votes = {{.description = "finalizer", .voted_for_block_num = 41U}},
   };
   BOOST_CHECK(forge::raw::unpack_exact<protocol::finalizer_info_response>(forge::raw::pack(finalizers)) == finalizers);

   const auto information = protocol::info_response{
       .resource_config = protocol::resource_limits_config{.id = protocol::resource_config_id{1U}},
       .resource_state = protocol::resource_limits_state{.id = protocol::resource_state_id{2U}},
   };
   BOOST_CHECK(forge::raw::unpack_exact<protocol::info_response>(forge::raw::pack(information)) == information);

   const auto corrections = protocol::ram_corrections_response{
       .rows = {{.id = protocol::account_ram_correction_id{3U},
                 .name = protocol::account_name{"alice"},
                 .ram_correction = 4U}},
       .next = protocol::account_name{"bob"},
   };
   BOOST_CHECK(forge::raw::unpack_exact<protocol::ram_corrections_response>(forge::raw::pack(corrections)) ==
               corrections);

   const auto producer_json = forge::codec::json::write(producer_request);
   BOOST_REQUIRE(producer_json.ok());
   const auto producer_value = forge::codec::json::read_value(producer_json.text);
   BOOST_REQUIRE(producer_value.ok());
   BOOST_TEST(!producer_value.value.get_object().contains("json"));
   BOOST_TEST(producer_value.value["lower_bound"].as_string() == "alice");
   BOOST_TEST(producer_value.value["cursor"][std::size_t{2}].as_uint64() == 0xffU);

   const auto response_json = forge::codec::json::write(producer_response);
   BOOST_REQUIRE(response_json.ok());
   const auto response_value = forge::codec::json::read_value(response_json.text);
   BOOST_REQUIRE(response_value.ok());
   BOOST_TEST(response_value.value["total_vote_weight"].as_double() == 1.5);
   BOOST_TEST(response_value.value["rows"][std::size_t{0}]["owner"].as_string() == "alice");
   BOOST_TEST(response_value.value["next"][std::size_t{1}].as_uint64() == 0x02U);
}

BOOST_AUTO_TEST_CASE(producer_frames_inherit_configured_raw_allocation_budgets) {
   const auto make_producer = [](std::string_view owner, std::size_t key_count) {
      auto authority = protocol::block_signing_authority_v0{.threshold = 1U};
      authority.keys.resize(key_count);
      auto producer = protocol::producer_info{.owner = protocol::account_name{owner}};
      producer.producer_authority = protocol::block_signing_authority{authority};
      return producer;
   };

   const auto oversized_container = forge::raw::pack(protocol::producers_response{
       .rows = {make_producer("alice", 2U)},
   });
   BOOST_CHECK_THROW((void)forge::raw::unpack_exact<protocol::producers_response>(
                         std::span<const std::uint8_t>{oversized_container},
                         forge::raw::unpack_limits{.max_container_elements = 1U,
                                                   .max_total_container_elements = 8U,
                                                   .max_bytes = 1'024U,
                                                   .first_container_elements = 1U}),
                     forge::raw::exceptions::allocation_limit);

   const auto cumulative = protocol::producers_response{
       .rows = {make_producer("alice", 1U), make_producer("bob", 1U)},
   };
   const auto cumulative_bytes = forge::raw::pack(cumulative);
   const auto accepted_limits = forge::raw::unpack_limits{.max_container_elements = 2U,
                                                          .max_total_container_elements = 4U,
                                                          .max_bytes = 1'024U,
                                                          .first_container_elements = 2U};
   BOOST_CHECK(forge::raw::pack(forge::raw::unpack_exact<protocol::producers_response>(
                   std::span<const std::uint8_t>{cumulative_bytes}, accepted_limits)) == cumulative_bytes);

   auto exhausted_limits = accepted_limits;
   exhausted_limits.max_total_container_elements = 3U;
   BOOST_CHECK_THROW((void)forge::raw::unpack_exact<protocol::producers_response>(
                         std::span<const std::uint8_t>{cumulative_bytes}, exhausted_limits),
                     forge::raw::exceptions::allocation_limit);
}

BOOST_AUTO_TEST_CASE(transaction_trace_is_one_typed_protocol_record_across_api_surfaces) {
   auto trace = protocol::transaction_trace{};
   trace.id._hash[0] = 0x42U;
   trace.cpu_usage_us = 17U;
   trace.net_usage = 32U;
   trace.actions.push_back({
       .action = protocol::action{{protocol::permission_level{protocol::account_name{"alice"},
                                                              protocol::permission_name{"active"}}},
                                  protocol::account_name{"eosio.token"},
                                  protocol::action_name{"transfer"},
                                  protocol::bytes{0x01U, 0x02U}},
       .receipt = protocol::action_receipt{.receiver = protocol::account_name{"eosio.token"}},
       .console = "executed",
   });
   trace.error = protocol::transaction_error{
       .category = "test",
       .code = 7,
       .message = "typed failure",
   };

   const auto encoded = forge::codec::json::write(trace);
   BOOST_REQUIRE(encoded.ok());
   const auto decoded = forge::codec::json::read<protocol::transaction_trace>(
       encoded.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(decoded.ok());
   BOOST_CHECK(decoded.value == trace);

   auto submitted = protocol::transaction_submit_response{.id = trace.id, .trace = trace};
   auto status = protocol::transaction_status_response{.id = trace.id, .trace = trace};
   auto read_only = protocol::transaction_read_only_response{.id = trace.id, .trace = trace};
   BOOST_CHECK(submitted.trace == status.trace);
   BOOST_CHECK(read_only.trace == *submitted.trace);
}
