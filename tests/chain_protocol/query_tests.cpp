#include <boost/test/unit_test.hpp>

#include <cstdint>

import forge.chain.protocol.state_query;
import forge.codec.json;
import forge.crypto.digest.sha256;

namespace protocol = forge::chain::protocol;

BOOST_AUTO_TEST_CASE(table_rows_defaults_to_spring_binary_output) {
   const auto request = protocol::table_rows_request{};
   BOOST_TEST(!request.json);
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
   response.rows = {{
       .code = protocol::name{"eosio.token"},
       .scope = protocol::name{"alice"},
       .table = protocol::name{"accounts"},
       .payer = protocol::account_name{"eosio.token"},
       .count = 1U,
   }};
   response.next = protocol::bytes{0x01U, 0x02U, 0x03U};
   const auto response_json = forge::codec::json::write(response);
   BOOST_REQUIRE(response_json.ok());
   const auto response_value = forge::codec::json::read_value(response_json.text);
   BOOST_REQUIRE(response_value.ok());
   const auto& object = response_value.value.get_object();
   BOOST_TEST(object.contains("next"));
   BOOST_TEST(!object.contains("more"));
   BOOST_TEST(!object.contains("next_key"));

   const auto exact_response = forge::codec::json::read<protocol::table_scope_response>(
       response_json.text, {.described_records = forge::codec::json::described_record_policy::exact});
   BOOST_REQUIRE(exact_response.ok());
   BOOST_CHECK(exact_response.value == response);
}
