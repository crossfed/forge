#include <boost/test/unit_test.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <algorithm>
#include <future>
#include <initializer_list>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

#include <forge/exceptions/macros.hpp>

import forge.api.core.registry;
import forge.api.http.mapping;
import forge.api.http.openapi;
import forge.chain.api.admin;
import forge.chain.api.block;
import forge.chain.api.exceptions;
import forge.chain.api.info;
import forge.chain.api.raw_client;
import forge.chain.api.state;
import forge.chain.api.transaction;
import forge.chain.api.verified_client;
import forge.chain.protocol.audit;
import forge.net.http.types;

namespace {

using forge::api::http::cache_policy;
using forge::api::http::route;
using forge::net::http::method;

template <typename T> T run(boost::asio::awaitable<T> operation) {
   auto context = boost::asio::io_context{};
   auto result = boost::asio::co_spawn(context, std::move(operation), boost::asio::use_future);
   context.run();
   return result.get();
}

class block_service final : public forge::chain::api::block {
 public:
   explicit block_service(forge::chain::protocol::block_response response) : response_{std::move(response)} {}

   boost::asio::awaitable<forge::chain::protocol::block_response>
   get_block(forge::chain::protocol::block_request) override {
      co_return response_;
   }

   boost::asio::awaitable<forge::chain::protocol::block_header_response>
   get_header(forge::chain::protocol::block_request) override {
      co_return forge::chain::protocol::block_header_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::block_state_response>
   get_block_state(forge::chain::protocol::block_request) override {
      co_return forge::chain::protocol::block_state_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::block_range_response>
   get_canonical_range(forge::chain::protocol::block_range_request) override {
      co_return forge::chain::protocol::block_range_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::protocol_features_response>
   get_activated_protocol_features(forge::chain::protocol::protocol_features_request) override {
      co_return forge::chain::protocol::protocol_features_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::consensus_parameters_response>
   get_consensus_parameters(forge::chain::protocol::anchored_request) override {
      co_return forge::chain::protocol::consensus_parameters_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::producers_response>
   get_producers(forge::chain::protocol::producers_request) override {
      co_return forge::chain::protocol::producers_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::producer_schedule_response>
   get_producer_schedule(forge::chain::protocol::anchored_request) override {
      co_return forge::chain::protocol::producer_schedule_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::finalizer_info_response>
   get_finalizer_info(forge::chain::protocol::anchored_request) override {
      co_return forge::chain::protocol::finalizer_info_response{};
   }

 private:
   forge::chain::protocol::block_response response_;
};

class state_service final : public forge::chain::api::state {
 public:
   explicit state_service(forge::chain::protocol::state_changes_response response) : response_{std::move(response)} {}

   boost::asio::awaitable<forge::chain::protocol::state_point_response>
   get_point(forge::chain::protocol::state_point_request) override {
      co_return forge::chain::protocol::state_point_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::state_range_response>
   get_range(forge::chain::protocol::state_range_request) override {
      co_return forge::chain::protocol::state_range_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::state_changes_response>
   get_changes(forge::chain::protocol::state_changes_request) override {
      co_return response_;
   }

   boost::asio::awaitable<forge::chain::protocol::account_response>
   get_account(forge::chain::protocol::account_request) override {
      co_return forge::chain::protocol::account_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::code_response>
   get_code(forge::chain::protocol::code_request) override {
      co_return forge::chain::protocol::code_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::table_rows_response>
   get_table_rows(forge::chain::protocol::table_rows_request) override {
      co_return forge::chain::protocol::table_rows_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::table_scope_response>
   get_table_scope(forge::chain::protocol::table_scope_request) override {
      co_return forge::chain::protocol::table_scope_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::currency_balance_response>
   get_currency_balance(forge::chain::protocol::currency_balance_request) override {
      co_return forge::chain::protocol::currency_balance_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::currency_stats_response>
   get_currency_stats(forge::chain::protocol::currency_stats_request) override {
      co_return forge::chain::protocol::currency_stats_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::scheduled_response>
   get_scheduled_transactions(forge::chain::protocol::scheduled_request) override {
      co_return forge::chain::protocol::scheduled_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::authorizers_response>
   get_accounts_by_authorizers(forge::chain::protocol::authorizers_request) override {
      co_return forge::chain::protocol::authorizers_response{};
   }

 private:
   forge::chain::protocol::state_changes_response response_;
};

class accepting_audit_verifier final : public forge::chain::api::audit_verifier {
 public:
   void verify_context(const forge::chain::protocol::response_context&) override {}
   void verify_finality(const forge::chain::protocol::state_anchor&,
                        const forge::chain::protocol::proof_blob&) override {}
   void verify_state_point(const forge::chain::protocol::state_anchor&,
                           const forge::chain::protocol::state_point_request&,
                           const std::optional<forge::chain::protocol::bytes>&,
                           const forge::chain::protocol::proof_blob&) override {}
   void verify_state_range(const forge::chain::protocol::state_anchor&,
                           const forge::chain::protocol::state_range_request&,
                           const forge::chain::protocol::state_range_response&,
                           const forge::chain::protocol::proof_blob&) override {}
   void verify_state_changes(const forge::chain::protocol::state_anchor&, const forge::chain::protocol::key_range&,
                             std::uint32_t, const forge::chain::protocol::state_change_range&,
                             const forge::chain::protocol::proof_blob&) override {}
   void verify_transaction(const forge::chain::protocol::state_anchor&, const forge::chain::protocol::transaction_id&,
                           const forge::chain::protocol::transaction_status_response&,
                           const forge::chain::protocol::transaction_inclusion_proof&) override {}
};

const route& find_route(const std::vector<route>& routes, std::string_view name) {
   const auto result = std::ranges::find(routes, name, &route::method_name);
   BOOST_REQUIRE(result != routes.end());
   return *result;
}

void require_routes(const std::vector<route>& routes, method verb, std::initializer_list<std::string_view> names) {
   for (const auto name : names) {
      const auto& value = find_route(routes, name);
      BOOST_TEST(value.verb == verb);
      if (verb == method::get) {
         BOOST_TEST(static_cast<int>(value.cache) == static_cast<int>(cache_policy::no_store));
      }
   }
}

} // namespace

BOOST_AUTO_TEST_CASE(chain_http_uses_resource_verbs) {
   const auto info = forge::api::http::traits<forge::chain::api::info>::routes();
   const auto blocks = forge::api::http::traits<forge::chain::api::block>::routes();
   const auto state = forge::api::http::traits<forge::chain::api::state>::routes();
   const auto transactions = forge::api::http::traits<forge::chain::api::transaction>::routes();
   const auto admin = forge::api::http::traits<forge::chain::api::admin>::routes();

   require_routes(info, method::get, {"get"});
   require_routes(blocks, method::get,
                  {"get_block", "get_header", "get_block_state", "get_canonical_range",
                   "get_activated_protocol_features", "get_consensus_parameters", "get_producers",
                   "get_producer_schedule", "get_finalizer_info"});
   require_routes(state, method::get,
                  {"get_account", "get_code", "get_table_rows", "get_table_scope", "get_currency_balance",
                   "get_currency_stats", "get_scheduled_transactions"});
   require_routes(state, method::post, {"get_point", "get_range", "get_changes", "get_accounts_by_authorizers"});
   require_routes(transactions, method::get, {"get_status", "await_transaction"});
   require_routes(transactions, method::post,
                  {"submit", "submit_batch", "get_required_keys", "compute_transaction", "send_read_only_transaction"});
   require_routes(admin, method::get,
                  {"producer_status", "supported_protocol_features", "account_ram_corrections",
                   "unapplied_transactions", "snapshot_requests", "integrity_hash"});
   require_routes(admin, method::post, {"push_block", "create_snapshot", "prune", "schedule_snapshot"});
   require_routes(admin, method::put, {"configure_pause", "set_access_policy", "schedule_protocol_features"});
   require_routes(admin, method::patch, {"update_runtime_options", "update_greylist"});
   require_routes(admin, method::delete_, {"unschedule_snapshot"});
}

BOOST_AUTO_TEST_CASE(chain_http_omits_an_unspecified_anchor) {
   const auto routes = forge::api::http::traits<forge::chain::api::block>::routes();
   const auto& route = find_route(routes, "get_consensus_parameters");
   const auto target = forge::api::http::detail::render_route_target(
       route, forge::chain::protocol::anchored_request{.anchor = std::nullopt,
                                                       .audit = forge::chain::protocol::audit_mode::required});

   BOOST_TEST(target == "/v1/chain/blocks/consensus-parameters?audit=required");
}

BOOST_AUTO_TEST_CASE(chain_openapi_uses_canonical_public_key_json_shape) {
   const auto document = forge::api::http::openapi<forge::chain::api::transaction>();
   const auto& schema = document["paths"]["/v1/chain/transactions/required-keys"]["post"]["responses"]["200"]["content"]
                                ["application/json"]["schema"]["items"];

   BOOST_TEST(schema["type"].as_string() == "string");
   BOOST_TEST(schema["format"].as_string() == "forge-public-key");
}

BOOST_AUTO_TEST_CASE(verified_block_response_is_bound_to_the_requested_identity) {
   auto response = forge::chain::protocol::block_response{};
   response.id = response.block.calculate_id();
   response.num = response.block.calculate_block_num();
   response.context = forge::chain::protocol::response_context{
       .chain = {},
       .head = response.id,
       .finalized = response.id,
       .anchor =
           forge::chain::protocol::state_anchor{
               .chain = {},
               .block = response.id,
               .block_num = response.num,
           },
   };
   response.audit = forge::chain::protocol::audit_bundle{
       .finality =
           forge::chain::protocol::proof_blob{
               .scheme = "test.finality",
               .version = 1,
           },
   };

   auto services = forge::api::core::registry{};
   services.install<forge::chain::api::block>(std::make_shared<block_service>(response));
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .blocks = services.get<forge::chain::api::block>(forge::chain::api::block::ref()),
       }},
       std::make_shared<accepting_audit_verifier>(),
   };

   auto other = response.id;
   ++other._hash[1];
   BOOST_CHECK_THROW(run(client.get_block({.id = other})), forge::chain::api::exceptions::invalid_finality);
   BOOST_CHECK_THROW(run(client.get_block({.num = response.num + 1U})),
                     forge::chain::api::exceptions::invalid_finality);
   const auto verified = run(client.get_block({.id = response.id, .num = response.num}));
   BOOST_TEST(verified.id == response.id);
}

BOOST_AUTO_TEST_CASE(verified_changes_cover_the_requested_interval_and_terminal_anchor) {
   auto first = forge::chain::protocol::state_anchor{.block_num = 11U};
   first.block._hash[0] = 11U;
   auto second = forge::chain::protocol::state_anchor{.block_num = 12U};
   second.block._hash[0] = 12U;

   const auto make_response = [&] {
      auto response = forge::chain::protocol::state_changes_response{};
      response.context.anchor = second;
      response.blocks = {
          {.anchor = first, .ranges = {{.range = {}}}},
          {.anchor = second, .ranges = {{.range = {}}}},
      };
      response.audit = forge::chain::protocol::audit_bundle{
          .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
          .state =
              {
                  forge::chain::protocol::proof_blob{.scheme = "test.changes"},
                  forge::chain::protocol::proof_blob{.scheme = "test.changes"},
              },
      };
      return response;
   };
   const auto request = forge::chain::protocol::state_changes_request{
       .from_block = 10U,
       .to_block = 12U,
   };
   const auto verify = [&](forge::chain::protocol::state_changes_response response) {
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::state>(std::make_shared<state_service>(std::move(response)));
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
          }},
          std::make_shared<accepting_audit_verifier>(),
      };
      return run(client.get_changes(request));
   };

   const auto valid = verify(make_response());
   BOOST_TEST(valid.blocks.size() == 2U);

   auto omitted = make_response();
   omitted.blocks.erase(omitted.blocks.begin());
   omitted.audit->state.erase(omitted.audit->state.begin());
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(omitted))), forge::chain::api::exceptions::invalid_state_proof);

   auto forged_terminal = make_response();
   forged_terminal.blocks.back().anchor.state_root._hash[0] = 99U;
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(forged_terminal))),
                     forge::chain::api::exceptions::invalid_state_proof);
}
