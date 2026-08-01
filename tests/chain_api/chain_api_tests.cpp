#include <boost/test/unit_test.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <concepts>
#include <future>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
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
import forge.chain.api.finality;
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

static_assert(std::same_as<decltype(forge::chain::protocol::transaction_read_only_request{}.transaction),
                           forge::chain::protocol::packed_transaction>);

template <typename T> T run(boost::asio::awaitable<T> operation) {
   auto context = boost::asio::io_context{};
   auto result = boost::asio::co_spawn(context, std::move(operation), boost::asio::use_future);
   context.run();
   return result.get();
}

class block_service final : public forge::chain::api::block {
 public:
   explicit block_service(forge::chain::protocol::block_response response) : response_{std::move(response)} {}
   explicit block_service(forge::chain::protocol::block_header_response response)
       : header_response_{std::move(response)} {}

   boost::asio::awaitable<forge::chain::protocol::block_response>
   get_block(forge::chain::protocol::block_request) override {
      co_return response_;
   }

   boost::asio::awaitable<forge::chain::protocol::block_header_response>
   get_header(forge::chain::protocol::block_request) override {
      co_return header_response_;
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
   forge::chain::protocol::block_header_response header_response_;
};

class info_service final : public forge::chain::api::info {
 public:
   explicit info_service(forge::chain::protocol::info_response response) : response_{std::move(response)} {}

   boost::asio::awaitable<forge::chain::protocol::info_response>
   get(forge::chain::protocol::anchored_request) override {
      co_return response_;
   }

 private:
   forge::chain::protocol::info_response response_;
};

class state_service final : public forge::chain::api::state {
 public:
   explicit state_service(forge::chain::protocol::state_changes_response response) : response_{std::move(response)} {}
   explicit state_service(forge::chain::protocol::state_point_response response)
       : point_response_{std::move(response)} {}
   explicit state_service(forge::chain::protocol::state_range_response response)
       : range_response_{std::move(response)} {}
   explicit state_service(forge::chain::protocol::account_response response) : account_response_{std::move(response)} {}

   boost::asio::awaitable<forge::chain::protocol::state_point_response>
   get_point(forge::chain::protocol::state_point_request) override {
      co_return point_response_;
   }

   boost::asio::awaitable<forge::chain::protocol::state_range_response>
   get_range(forge::chain::protocol::state_range_request) override {
      co_return range_response_;
   }

   boost::asio::awaitable<forge::chain::protocol::state_changes_response>
   get_changes(forge::chain::protocol::state_changes_request) override {
      co_return response_;
   }

   boost::asio::awaitable<forge::chain::protocol::account_response>
   get_account(forge::chain::protocol::account_request) override {
      co_return account_response_;
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
   forge::chain::protocol::state_point_response point_response_;
   forge::chain::protocol::state_range_response range_response_;
   forge::chain::protocol::state_changes_response response_;
   forge::chain::protocol::account_response account_response_;
};

class transaction_service final : public forge::chain::api::transaction {
 public:
   explicit transaction_service(forge::chain::protocol::transaction_status_response response)
       : response_{std::move(response)} {}

   boost::asio::awaitable<forge::chain::protocol::transaction_submit_response>
   submit(forge::chain::protocol::transaction_submit_request) override {
      co_return forge::chain::protocol::transaction_submit_response{};
   }

   boost::asio::awaitable<std::vector<forge::chain::protocol::transaction_submit_response>>
   submit_batch(std::vector<forge::chain::protocol::transaction_submit_request>) override {
      co_return std::vector<forge::chain::protocol::transaction_submit_response>{};
   }

   boost::asio::awaitable<forge::chain::protocol::transaction_status_response>
   get_status(forge::chain::protocol::transaction_status_request) override {
      co_return response_;
   }

   boost::asio::awaitable<forge::chain::protocol::transaction_status_response>
   await_transaction(forge::chain::protocol::transaction_await_request) override {
      co_return response_;
   }

   boost::asio::awaitable<std::vector<forge::chain::protocol::public_key>>
   get_required_keys(forge::chain::protocol::transaction_required_keys_request) override {
      co_return std::vector<forge::chain::protocol::public_key>{};
   }

   boost::asio::awaitable<forge::chain::protocol::transaction_read_only_response>
   compute_transaction(forge::chain::protocol::transaction_read_only_request) override {
      co_return forge::chain::protocol::transaction_read_only_response{};
   }

   boost::asio::awaitable<forge::chain::protocol::transaction_read_only_response>
   send_read_only_transaction(forge::chain::protocol::transaction_read_only_request) override {
      co_return forge::chain::protocol::transaction_read_only_response{};
   }

 private:
   forge::chain::protocol::transaction_status_response response_;
};

class accepting_audit_verifier final : public forge::chain::api::audit_verifier {
 public:
   void verify_context(const forge::chain::protocol::response_context&) override {}
   void verify_finality(const forge::chain::protocol::state_anchor&,
                        const forge::chain::protocol::proof_blob&) override {}
   void verify_state_point(const forge::chain::protocol::state_anchor&,
                           const forge::chain::protocol::state_point_request&,
                           const std::optional<forge::chain::protocol::bytes>&,
                           const forge::chain::protocol::proof_blob&) override {
      ++state_point_verifications;
   }
   void verify_state_range(const forge::chain::protocol::state_anchor&,
                           const forge::chain::protocol::state_range_request&,
                           const forge::chain::protocol::state_range_response&,
                           const forge::chain::protocol::proof_blob&) override {
      ++state_range_verifications;
   }
   void verify_state_changes(const forge::chain::protocol::state_anchor&, const forge::chain::protocol::key_range&,
                             std::uint32_t, const forge::chain::protocol::state_change_range&,
                             const forge::chain::protocol::proof_blob&) override {
      ++state_change_verifications;
   }
   void verify_ancestry(const forge::chain::protocol::state_anchor& finalized,
                        std::span<const forge::chain::protocol::state_anchor> intermediate,
                        const forge::chain::protocol::proof_blob& proof) override {
      ++ancestry_verifications;
      ancestry_finalized = finalized;
      ancestry_intermediate.assign(intermediate.begin(), intermediate.end());
      ancestry_proof = proof;
   }
   void verify_transaction(const forge::chain::protocol::state_anchor&, const forge::chain::protocol::transaction_id&,
                           const forge::chain::protocol::transaction_status_response&,
                           const forge::chain::protocol::transaction_inclusion_proof&) override {
      ++transaction_verifications;
   }

   std::size_t state_point_verifications = 0;
   std::size_t state_range_verifications = 0;
   std::size_t state_change_verifications = 0;
   std::size_t transaction_verifications = 0;
   std::size_t ancestry_verifications = 0;
   std::optional<forge::chain::protocol::state_anchor> ancestry_finalized;
   std::vector<forge::chain::protocol::state_anchor> ancestry_intermediate;
   std::optional<forge::chain::protocol::proof_blob> ancestry_proof;
};

class account_projection_verifier final : public forge::chain::api::projection_verifier {
 public:
   void verify(const forge::chain::protocol::account_request& request,
               const forge::chain::protocol::account_response& response,
               const forge::chain::protocol::audit_bundle& audit,
               forge::chain::api::audit_verifier& verifier) override {
      if (!response.context.anchor || audit.state.size() != 1U) {
         FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::invalid_state_proof,
                               "test account projection requires one authenticated source");
      }
      ++verifications;
      verifier.verify_state_point(*response.context.anchor,
                                  forge::chain::protocol::state_point_request{
                                      .key = {9U},
                                      .anchor = request.anchor,
                                      .audit = forge::chain::protocol::audit_mode::required,
                                  },
                                  forge::chain::protocol::bytes{1U, 2U}, audit.state.front());
   }

   std::size_t verifications = 0;
};

class recording_finality_verifier final : public forge::chain::api::finality_verifier {
 public:
   void verify(const forge::chain::protocol::state_anchor&, const forge::chain::protocol::proof_blob&) override {
      ++verify_calls;
      if (failures_remaining != 0U) {
         --failures_remaining;
         FORGE_THROW_EXCEPTION(forge::chain::api::exceptions::invalid_finality,
                               "test finality delegate rejected anchor");
      }
   }

   void verify_ancestry(const forge::chain::protocol::state_anchor& finalized,
                        std::span<const forge::chain::protocol::state_anchor> intermediate,
                        const forge::chain::protocol::proof_blob& proof) override {
      ++ancestry_calls;
      ancestry_finalized = finalized;
      ancestry_intermediate.assign(intermediate.begin(), intermediate.end());
      ancestry_proof = proof;
   }

   std::size_t verify_calls = 0;
   std::size_t ancestry_calls = 0;
   std::size_t failures_remaining = 0;
   std::optional<forge::chain::protocol::state_anchor> ancestry_finalized;
   std::vector<forge::chain::protocol::state_anchor> ancestry_intermediate;
   std::optional<forge::chain::protocol::proof_blob> ancestry_proof;
};

class blocking_finality_verifier final : public forge::chain::api::finality_verifier {
 public:
   void verify(const forge::chain::protocol::state_anchor&, const forge::chain::protocol::proof_blob&) override {
      const auto call = verify_calls.fetch_add(1U) + 1U;
      if (call != 1U) {
         return;
      }

      auto lock = std::unique_lock{mutex_};
      entered_ = true;
      entered_condition_.notify_all();
      release_condition_.wait(lock, [this] { return released_; });
   }

   void verify_ancestry(const forge::chain::protocol::state_anchor&,
                        std::span<const forge::chain::protocol::state_anchor>,
                        const forge::chain::protocol::proof_blob&) override {}

   void wait_until_entered() {
      auto lock = std::unique_lock{mutex_};
      entered_condition_.wait(lock, [this] { return entered_; });
   }

   void release() {
      {
         const auto lock = std::lock_guard{mutex_};
         released_ = true;
      }
      release_condition_.notify_all();
   }

   std::atomic<std::size_t> verify_calls = 0;

 private:
   std::mutex mutex_;
   std::condition_variable entered_condition_;
   std::condition_variable release_condition_;
   bool entered_ = false;
   bool released_ = false;
};

forge::chain::protocol::state_anchor make_finality_anchor() {
   auto anchor = forge::chain::protocol::state_anchor{};
   anchor.chain._hash[0] = 1U;
   anchor.block._hash[0] = 2U;
   anchor.block_num = 3U;
   anchor.transaction_root._hash[0] = 4U;
   anchor.state_root._hash[0] = 5U;
   anchor.state_size = 6U;
   anchor.change_root._hash[0] = 7U;
   anchor.change_count = 8U;
   return anchor;
}

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
   response.canonical = true;
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

BOOST_AUTO_TEST_CASE(verified_block_rejects_transaction_receipts_not_committed_by_its_header) {
   auto response = forge::chain::protocol::block_response{};
   auto receipt = forge::chain::protocol::transaction_receipt{};
   receipt.status = forge::chain::protocol::transaction_receipt::status::executed;
   receipt.cpu_usage_us = 7U;
   auto receipt_id = forge::chain::protocol::transaction_id{};
   receipt_id._hash[0] = 17U;
   receipt.trx = receipt_id;
   response.block.transactions.push_back(receipt);
   response.block.transaction_mroot = forge::chain::protocol::calculate_transaction_mroot(response.block.transactions);
   response.id = response.block.calculate_id();
   response.num = response.block.calculate_block_num();
   response.canonical = true;
   response.context = forge::chain::protocol::response_context{
       .head = response.id,
       .finalized = response.id,
       .anchor =
           forge::chain::protocol::state_anchor{
               .block = response.id,
               .block_num = response.num,
               .transaction_root = response.block.transaction_mroot,
           },
   };
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
   };

   auto mutated = response;
   ++mutated.block.transactions.front().cpu_usage_us;
   auto services = forge::api::core::registry{};
   services.install<forge::chain::api::block>(std::make_shared<block_service>(std::move(mutated)));
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .blocks = services.get<forge::chain::api::block>(forge::chain::api::block::ref()),
       }},
       std::make_shared<accepting_audit_verifier>(),
   };

   BOOST_CHECK_THROW(run(client.get_block({.id = response.id, .num = response.num})),
                     forge::chain::api::exceptions::invalid_finality);
}

BOOST_AUTO_TEST_CASE(verified_header_is_bound_to_its_request_and_finalized_anchor) {
   auto response = forge::chain::protocol::block_header_response{};
   response.header.transaction_mroot._hash[0] = 13U;
   response.id = response.header.calculate_id();
   response.num = response.header.calculate_block_num();
   response.canonical = true;
   response.context.anchor = forge::chain::protocol::state_anchor{
       .block = response.id,
       .block_num = response.num,
       .transaction_root = response.header.transaction_mroot,
   };
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
   };

   const auto verify = [&](forge::chain::protocol::block_header_response candidate) {
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::block>(std::make_shared<block_service>(std::move(candidate)));
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .blocks = services.get<forge::chain::api::block>(forge::chain::api::block::ref()),
          }},
          std::make_shared<accepting_audit_verifier>(),
      };
      return run(client.get_header({.id = response.id, .num = response.num}));
   };

   BOOST_TEST(verify(response).id == response.id);

   auto mutated = response;
   ++mutated.header.transaction_mroot._hash[0];
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(mutated))), forge::chain::api::exceptions::invalid_finality);

   auto non_canonical = response;
   non_canonical.canonical = false;
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(non_canonical))),
                     forge::chain::api::exceptions::invalid_finality);
}

BOOST_AUTO_TEST_CASE(verified_raw_state_queries_delegate_content_proofs) {
   auto anchor = forge::chain::protocol::state_anchor{};
   anchor.block._hash[0] = 21U;
   anchor.block_num = 21U;
   const auto audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .state = {forge::chain::protocol::proof_blob{.scheme = "test.state"}},
   };

   {
      auto response = forge::chain::protocol::state_point_response{};
      response.context.anchor = anchor;
      response.audit = audit;
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::state>(std::make_shared<state_service>(std::move(response)));
      auto verifier = std::make_shared<accepting_audit_verifier>();
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
          }},
          verifier,
      };

      static_cast<void>(run(client.get_point({.key = {1U}, .anchor = anchor.block})));
      BOOST_TEST(verifier->state_point_verifications == 1U);
   }

   {
      auto response = forge::chain::protocol::state_range_response{};
      response.context.anchor = anchor;
      response.audit = audit;
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::state>(std::make_shared<state_service>(std::move(response)));
      auto verifier = std::make_shared<accepting_audit_verifier>();
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
          }},
          verifier,
      };

      static_cast<void>(run(client.get_range({.anchor = anchor.block})));
      BOOST_TEST(verifier->state_range_verifications == 1U);
   }
}

BOOST_AUTO_TEST_CASE(verified_info_rejects_payload_identity_inconsistent_with_audited_context) {
   auto chain = forge::chain::protocol::chain_id{};
   chain._hash[0] = 1U;
   const auto finalized_header = forge::chain::protocol::signed_block_header{};
   const auto finalized = finalized_header.calculate_id();
   auto head_header = forge::chain::protocol::signed_block_header{};
   head_header.previous = finalized;
   const auto head = head_header.calculate_id();

   auto response = forge::chain::protocol::info_response{};
   response.chain = chain;
   response.head = head;
   response.head_num = head_header.calculate_block_num();
   response.finalized = finalized;
   response.finalized_num = finalized_header.calculate_block_num();
   response.context = forge::chain::protocol::response_context{
       .chain = chain,
       .head = head,
       .finalized = finalized,
       .anchor =
           forge::chain::protocol::state_anchor{
               .chain = chain,
               .block = finalized,
               .block_num = response.finalized_num,
           },
   };
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
   };

   const auto verify = [](forge::chain::protocol::info_response candidate,
                          forge::chain::protocol::anchored_request request = {}) {
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::info>(std::make_shared<info_service>(std::move(candidate)));
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .information = services.get<forge::chain::api::info>(forge::chain::api::info::ref()),
          }},
          std::make_shared<accepting_audit_verifier>(),
      };
      return run(client.get_info(std::move(request)));
   };

   BOOST_TEST(verify(response).chain == chain);

   auto wrong_anchor = finalized;
   ++wrong_anchor._hash[0];
   BOOST_CHECK_THROW(static_cast<void>(verify(response, {.anchor = wrong_anchor})),
                     forge::chain::api::exceptions::invalid_finality);

   auto wrong_chain = response;
   ++wrong_chain.chain._hash[0];
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(wrong_chain))), forge::chain::api::exceptions::wrong_chain);

   auto wrong_head = response;
   ++wrong_head.head._hash[0];
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(wrong_head))), forge::chain::api::exceptions::invalid_finality);

   auto wrong_head_num = response;
   ++wrong_head_num.head_num;
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(wrong_head_num))),
                     forge::chain::api::exceptions::invalid_finality);

   auto wrong_finalized = response;
   ++wrong_finalized.finalized._hash[0];
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(wrong_finalized))),
                     forge::chain::api::exceptions::invalid_finality);

   auto wrong_finalized_num = response;
   ++wrong_finalized_num.finalized_num;
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(wrong_finalized_num))),
                     forge::chain::api::exceptions::invalid_finality);
}

BOOST_AUTO_TEST_CASE(verified_await_transaction_enforces_requested_finality) {
   auto id = forge::chain::protocol::transaction_id{};
   id._hash[0] = 29U;
   auto anchor = forge::chain::protocol::state_anchor{};
   anchor.block._hash[0] = 42U;
   anchor.block_num = 42U;

   auto response = forge::chain::protocol::transaction_status_response{};
   response.id = id;
   response.state = forge::chain::protocol::transaction_lifecycle::included;
   response.context.anchor = anchor;
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .transaction = forge::chain::protocol::transaction_inclusion_proof{},
   };

   const auto await = [&](forge::chain::protocol::transaction_status_response candidate,
                          forge::chain::protocol::transaction_lifecycle desired) {
      auto services = forge::api::core::registry{};
      services.install<forge::chain::api::transaction>(std::make_shared<transaction_service>(std::move(candidate)));
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .transactions = services.get<forge::chain::api::transaction>(forge::chain::api::transaction::ref()),
          }},
          std::make_shared<accepting_audit_verifier>(),
      };
      return run(client.await_transaction({.id = id, .desired = desired}));
   };

   BOOST_CHECK_THROW(static_cast<void>(await(response, forge::chain::protocol::transaction_lifecycle::finalized)),
                     forge::chain::api::exceptions::invalid_transaction_proof);

   auto wrong_head = response;
   ++wrong_head.head._hash[0];
   BOOST_CHECK_THROW(
       static_cast<void>(await(std::move(wrong_head), forge::chain::protocol::transaction_lifecycle::included)),
       forge::chain::api::exceptions::invalid_finality);

   response.state = forge::chain::protocol::transaction_lifecycle::finalized;
   BOOST_TEST(static_cast<unsigned>(await(response, forge::chain::protocol::transaction_lifecycle::finalized).state) ==
              static_cast<unsigned>(forge::chain::protocol::transaction_lifecycle::finalized));
   BOOST_TEST(static_cast<unsigned>(await(response, forge::chain::protocol::transaction_lifecycle::included).state) ==
              static_cast<unsigned>(forge::chain::protocol::transaction_lifecycle::finalized));
}

BOOST_AUTO_TEST_CASE(verified_transaction_status_delegates_the_inclusion_proof) {
   auto id = forge::chain::protocol::transaction_id{};
   id._hash[0] = 31U;
   auto response = forge::chain::protocol::transaction_status_response{};
   response.id = id;
   response.state = forge::chain::protocol::transaction_lifecycle::finalized;
   response.context.anchor = forge::chain::protocol::state_anchor{.block_num = 7U};
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .transaction = forge::chain::protocol::transaction_inclusion_proof{},
   };

   auto services = forge::api::core::registry{};
   services.install<forge::chain::api::transaction>(std::make_shared<transaction_service>(std::move(response)));
   auto verifier = std::make_shared<accepting_audit_verifier>();
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .transactions = services.get<forge::chain::api::transaction>(forge::chain::api::transaction::ref()),
       }},
       verifier,
   };

   static_cast<void>(run(client.get_transaction_status({.id = id})));
   BOOST_TEST(verifier->transaction_verifications == 1U);
}

BOOST_AUTO_TEST_CASE(verified_composite_response_delegates_product_projection_and_authenticated_sources) {
   auto anchor = make_finality_anchor();
   auto response = forge::chain::protocol::account_response{};
   response.account = forge::chain::protocol::account_name{"alice"};
   response.context = forge::chain::protocol::response_context{
       .chain = anchor.chain,
       .head = anchor.block,
       .finalized = anchor.block,
       .anchor = anchor,
   };
   response.audit = forge::chain::protocol::audit_bundle{
       .finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"},
       .state = {forge::chain::protocol::proof_blob{.scheme = "test.state"}},
   };

   auto services = forge::api::core::registry{};
   services.install<forge::chain::api::state>(std::make_shared<state_service>(std::move(response)));
   auto audit = std::make_shared<accepting_audit_verifier>();
   auto projections = std::make_shared<account_projection_verifier>();
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{
           .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
       }},
       audit,
       projections,
   };

   const auto result =
       run(client.get_account({.account = forge::chain::protocol::account_name{"alice"}, .anchor = anchor.block}));

   BOOST_CHECK(result.account == forge::chain::protocol::account_name{"alice"});
   BOOST_TEST(projections->verifications == 1U);
   BOOST_TEST(audit->state_point_verifications == 1U);
}

BOOST_AUTO_TEST_CASE(verified_client_fails_closed_for_methods_without_content_witnesses) {
   auto client = forge::chain::api::verified_client{
       forge::chain::api::raw_client{forge::chain::api::service_handles{}},
       std::make_shared<accepting_audit_verifier>(),
   };

   BOOST_CHECK_THROW(run(client.get_block_state(forge::chain::protocol::block_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_canonical_range(forge::chain::protocol::block_range_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_activated_protocol_features(forge::chain::protocol::protocol_features_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_consensus_parameters(forge::chain::protocol::anchored_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_producers(forge::chain::protocol::producers_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_producer_schedule(forge::chain::protocol::anchored_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_finalizer_info(forge::chain::protocol::anchored_request{})),
                     forge::chain::api::exceptions::audit_not_supported);

   BOOST_CHECK_THROW(run(client.get_account(forge::chain::protocol::account_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_code(forge::chain::protocol::code_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_table_rows(forge::chain::protocol::table_rows_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_table_scope(forge::chain::protocol::table_scope_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_currency_balance(forge::chain::protocol::currency_balance_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_currency_stats(forge::chain::protocol::currency_stats_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_scheduled_transactions(forge::chain::protocol::scheduled_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.get_accounts_by_authorizers(forge::chain::protocol::authorizers_request{})),
                     forge::chain::api::exceptions::audit_not_supported);

   BOOST_CHECK_THROW(run(client.compute_transaction(forge::chain::protocol::transaction_read_only_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
   BOOST_CHECK_THROW(run(client.send_read_only_transaction(forge::chain::protocol::transaction_read_only_request{})),
                     forge::chain::api::exceptions::audit_not_supported);
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
      response.audit = forge::chain::protocol::audit_bundle{};
      response.audit->finality = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
      response.audit->ancestry = forge::chain::protocol::proof_blob{.scheme = "test.ancestry"};
      response.audit->state = {
          forge::chain::protocol::proof_blob{.scheme = "test.changes"},
          forge::chain::protocol::proof_blob{.scheme = "test.changes"},
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
      auto verifier = std::make_shared<accepting_audit_verifier>();
      auto client = forge::chain::api::verified_client{
          forge::chain::api::raw_client{forge::chain::api::service_handles{
              .state_queries = services.get<forge::chain::api::state>(forge::chain::api::state::ref()),
          }},
          verifier,
      };
      return std::pair{run(client.get_changes(request)), std::move(verifier)};
   };

   const auto valid = verify(make_response());
   BOOST_TEST(valid.first.blocks.size() == 2U);
   BOOST_TEST(valid.second->state_change_verifications == 2U);
   BOOST_TEST(valid.second->ancestry_verifications == 1U);
   BOOST_REQUIRE(valid.second->ancestry_finalized);
   BOOST_TEST(valid.second->ancestry_finalized->block == second.block);
   BOOST_REQUIRE_EQUAL(valid.second->ancestry_intermediate.size(), 1U);
   BOOST_TEST(valid.second->ancestry_intermediate.front().block == first.block);
   BOOST_REQUIRE(valid.second->ancestry_proof);
   BOOST_TEST(valid.second->ancestry_proof->scheme == "test.ancestry");

   auto missing_ancestry = make_response();
   missing_ancestry.audit->ancestry.reset();
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(missing_ancestry))),
                     forge::chain::api::exceptions::invalid_finality);

   auto omitted = make_response();
   omitted.blocks.erase(omitted.blocks.begin());
   omitted.audit->state.erase(omitted.audit->state.begin());
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(omitted))), forge::chain::api::exceptions::invalid_state_proof);

   auto forged_terminal = make_response();
   forged_terminal.blocks.back().anchor.state_root._hash[0] = 99U;
   BOOST_CHECK_THROW(static_cast<void>(verify(std::move(forged_terminal))),
                     forge::chain::api::exceptions::invalid_state_proof);
}

BOOST_AUTO_TEST_CASE(cached_finality_verifier_reuses_an_exact_anchor) {
   const auto anchor = make_finality_anchor();
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   auto delegate = std::make_shared<recording_finality_verifier>();
   auto verifier = forge::chain::api::cached_finality_verifier{delegate, 4U};

   verifier.verify(anchor, proof);
   verifier.verify(anchor, proof);

   BOOST_TEST(delegate->verify_calls == 1U);
}

BOOST_AUTO_TEST_CASE(cached_finality_verifier_single_flights_a_concurrent_exact_anchor) {
   const auto anchor = make_finality_anchor();
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   auto delegate = std::make_shared<blocking_finality_verifier>();
   auto verifier = forge::chain::api::cached_finality_verifier{delegate, 4U};

   auto first = std::async(std::launch::async, [&] { verifier.verify(anchor, proof); });
   delegate->wait_until_entered();

   auto second_started = std::promise<void>{};
   auto second_started_future = second_started.get_future();
   auto second = std::async(std::launch::async, [&] {
      second_started.set_value();
      verifier.verify(anchor, proof);
   });
   second_started_future.wait();
   const auto second_status = second.wait_for(std::chrono::milliseconds{100});

   delegate->release();
   first.get();
   second.get();

   BOOST_CHECK(second_status == std::future_status::timeout);
   BOOST_TEST(delegate->verify_calls.load() == 1U);
}

BOOST_AUTO_TEST_CASE(cached_finality_verifier_rejects_a_conflicting_anchor_identity) {
   const auto anchor = make_finality_anchor();
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   auto delegate = std::make_shared<recording_finality_verifier>();
   auto verifier = forge::chain::api::cached_finality_verifier{delegate, 4U};
   verifier.verify(anchor, proof);

   auto conflicting = anchor;
   ++conflicting.change_count;
   BOOST_CHECK_THROW(verifier.verify(conflicting, proof), forge::chain::api::exceptions::invalid_finality);
   BOOST_TEST(delegate->verify_calls == 1U);
}

BOOST_AUTO_TEST_CASE(cached_finality_verifier_does_not_cache_a_failed_verification) {
   const auto anchor = make_finality_anchor();
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.finality"};
   auto delegate = std::make_shared<recording_finality_verifier>();
   delegate->failures_remaining = 1U;
   auto verifier = forge::chain::api::cached_finality_verifier{delegate, 4U};

   BOOST_CHECK_THROW(verifier.verify(anchor, proof), forge::chain::api::exceptions::invalid_finality);
   BOOST_CHECK_NO_THROW(verifier.verify(anchor, proof));
   BOOST_CHECK_NO_THROW(verifier.verify(anchor, proof));
   BOOST_TEST(delegate->verify_calls == 2U);
}

BOOST_AUTO_TEST_CASE(cached_finality_verifier_delegates_ancestry_and_caches_the_finalized_anchor) {
   const auto finalized = make_finality_anchor();
   auto earlier = finalized;
   earlier.block._hash[0] = 1U;
   earlier.block_num = finalized.block_num - 1U;
   const auto intermediate = std::vector{earlier};
   const auto proof = forge::chain::protocol::proof_blob{.scheme = "test.ancestry", .version = 1U};
   auto delegate = std::make_shared<recording_finality_verifier>();
   auto verifier = forge::chain::api::cached_finality_verifier{delegate, 4U};

   verifier.verify_ancestry(finalized, std::span<const forge::chain::protocol::state_anchor>{intermediate}, proof);
   verifier.verify(finalized, proof);
   verifier.verify_ancestry(finalized, std::span<const forge::chain::protocol::state_anchor>{intermediate}, proof);

   BOOST_TEST(delegate->verify_calls == 0U);
   BOOST_TEST(delegate->ancestry_calls == 2U);
   BOOST_REQUIRE(delegate->ancestry_finalized);
   BOOST_TEST(delegate->ancestry_finalized->block == finalized.block);
   BOOST_REQUIRE_EQUAL(delegate->ancestry_intermediate.size(), 1U);
   BOOST_TEST(delegate->ancestry_intermediate.front().block == earlier.block);
   BOOST_REQUIRE(delegate->ancestry_proof);
   BOOST_TEST(delegate->ancestry_proof->scheme == proof.scheme);
}
