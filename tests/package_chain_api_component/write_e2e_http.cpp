module;

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>

#include <chrono>
#include <exception>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

module package.chain_api_component.write_e2e_http;

import package.chain_api_component.write_fixture;
import forge.api.core.binding;
import forge.api.core.exceptions;
import forge.api.core.handle;
import forge.api.core.registry;
import forge.api.http.binding;
import forge.api.http.proxy;
import forge.asio.blocking;
import forge.asio.exceptions;
import forge.asio.runtime;
import forge.chain.api.admin;
import forge.chain.api.exceptions;
import forge.chain.api.limits;
import forge.chain.api.submission;
import forge.chain.api.submission_client;
import forge.chain.api.transaction;
import forge.chain.protocol.admin;
import forge.chain.protocol.transaction_query;
import forge.net.http.base_url;
import forge.net.http.client;
import forge.net.http.router;
import forge.net.http.server;

namespace package_chain_api_component {

namespace {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

void wait_until(std::function<bool()> predicate, std::string_view failure) {
   const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
   while (!predicate() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
   }
   require(predicate(), failure);
}

void require_long_poll_transport(forge::asio::runtime& runtime,
                                 const forge::api::core::handle<chain_api::transaction>& remote,
                                 const write_services& services, std::string_view transport,
                                 bool require_remote_cancellation) {
   const auto deadlines_before = services.transaction_await_deadlines();
   auto deadline_observed = false;
   try {
      static_cast<void>(forge::asio::blocking::run(
          runtime, remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 10})));
   } catch (const forge::chain::api::exceptions::deadline_exceeded&) {
      deadline_observed = true;
   }
   require(deadline_observed, std::string{transport} + " long-poll ignored its request deadline");
   require(services.transaction_await_deadlines() == deadlines_before + 1U,
           std::string{transport} + " deadline did not originate at the owner");

   const auto started_before = services.transaction_await_started();
   const auto cancellations_before = services.transaction_await_cancellations();
   auto cancellation = boost::asio::cancellation_signal{};
   auto pending = boost::asio::co_spawn(
       runtime.context(), remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 300'000}),
       boost::asio::bind_cancellation_slot(cancellation.slot(), boost::asio::use_future));
   wait_until([&] { return services.transaction_await_started() > started_before; },
              std::string{transport} + " long-poll did not reach the owner");
   cancellation.emit(boost::asio::cancellation_type::all);
   auto caller_cancelled = false;
   try {
      static_cast<void>(pending.get());
   } catch (const forge::api::core::exceptions::cancelled&) {
      caller_cancelled = true;
   } catch (const forge::asio::exceptions::canceled&) {
      caller_cancelled = true;
   } catch (const std::exception& error) {
      require(false, std::string{transport} + " long-poll leaked a standard exception: " + error.what());
   }
   require(caller_cancelled, std::string{transport} + " long-poll did not return typed cancellation");
   if (require_remote_cancellation) {
      wait_until([&] { return services.transaction_await_cancellations() > cancellations_before; },
                 std::string{transport} + " long-poll cancellation did not reach the owner");
   }
   static_cast<void>(forge::asio::blocking::run(runtime, remote->get_status(protocol::transaction_status_request{})));
}

void require_http_submission_limits(forge::asio::runtime& runtime, forge::net::http::client& client,
                                    write_services& services) {
   auto limits_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::transaction>(client));
   auto submission_limits_remote =
       forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::submission>(client));
   const auto started_before = services.transaction_await_started();
   auto rejected = false;
   try {
      static_cast<void>(forge::asio::blocking::run(
          runtime, limits_remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 300'001})));
   } catch (const forge::chain::api::exceptions::resource_exhausted&) {
      rejected = true;
   }
   require(rejected, "HTTP owner boundary accepted an oversized await deadline");
   require(services.transaction_await_started() == started_before, "HTTP oversized await deadline reached the owner");

   const auto submission_calls_before = services.submission_calls();
   auto submit_rejected = false;
   try {
      static_cast<void>(
          forge::asio::blocking::run(runtime, submission_limits_remote->submit(protocol::transaction_submit_request{
                                                  .timeout_ms = package_limits().max_await_ms + 1U,
                                              })));
   } catch (const forge::chain::api::exceptions::resource_exhausted&) {
      submit_rejected = true;
   }
   require(submit_rejected, "HTTP owner boundary accepted an oversized submit deadline");

   auto zero_submit_rejected = false;
   try {
      static_cast<void>(forge::asio::blocking::run(
          runtime, submission_limits_remote->submit(protocol::transaction_submit_request{.timeout_ms = 0U})));
   } catch (const forge::chain::api::exceptions::invalid_request&) {
      zero_submit_rejected = true;
   }
   require(zero_submit_rejected, "HTTP owner boundary accepted a zero submit deadline");

   auto bounded_item = protocol::transaction_submit_request{.timeout_ms = 2'000U};
   auto bounded_transaction = protocol::signed_transaction{};
   bounded_transaction.expiration = protocol::time_point_sec{1U};
   bounded_item.transaction = protocol::packed_transaction{std::move(bounded_transaction)};
   const auto bounded_batch = forge::asio::blocking::run(
       runtime, submission_limits_remote->submit_batch(protocol::transaction_submit_batch_request{
                    .transactions = {std::move(bounded_item)},
                    .timeout_ms = 1'000U,
                }));
   require(bounded_batch.size() == 1U, "HTTP batch deadline cap changed response cardinality");
   require(services.submission_calls() == submission_calls_before + 1U,
           "HTTP batch deadline cap did not reach the owner");
   require(services.submission_last_batch_timeout_ms() == 1'000U,
           "HTTP batch deadline cap was not propagated to the owner");
}

} // namespace

write_responses run_http_write_e2e(write_services& services) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::core::registry{};
   apis.install<chain_api::transaction>(chain_api::limited_descriptor<chain_api::transaction>(package_limits()),
                                        services.transactions());
   apis.install<chain_api::submission>(chain_api::limited_descriptor<chain_api::submission>(package_limits()),
                                       services.submissions());
   apis.install<chain_api::admin>(chain_api::limited_descriptor<chain_api::admin>(package_limits()),
                                  services.administration());
   auto router = forge::net::http::router{};
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::transaction>()
                    .build());
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::submission>()
                    .build());
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::admin>()
                    .build());
   auto server = forge::net::http::server{
       runtime,
       forge::net::http::server_config{.max_request_body_bytes = 1U * 1024U * 1024U},
       std::move(router),
   };
   forge::asio::blocking::run(runtime, server.async_start());
   require(server.port() != 0, "HTTP chain API server did not bind");
   auto responses = write_responses{};
   try {
      auto client = forge::net::http::client{
          runtime,
          forge::net::http::parse_base_url("http://127.0.0.1:" + std::to_string(server.port())),
      };
      require_http_submission_limits(runtime, client, services);
      auto transaction_remote =
          forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::transaction>(client));
      auto submission_remote =
          forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::submission>(client));
      auto admin_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::admin>(client));
      responses.transaction = forge::asio::blocking::run(
          runtime, transaction_remote->compute_transaction(protocol::transaction_read_only_request{
                       .audit = protocol::audit_mode::required,
                   }));
      responses.administration =
          forge::asio::blocking::run(runtime, admin_remote->producer_status(protocol::admin_query{}));
      auto submission = chain_api::submission_client{std::move(submission_remote)};
      auto submitted = protocol::transaction_submit_request{.timeout_ms = 1'234U};
      submitted.transaction = protocol::packed_transaction{protocol::signed_transaction{}};
      const auto submitted_id = submitted.transaction.id();
      require(forge::asio::blocking::run(runtime, submission.submit(std::move(submitted))).id == submitted_id,
              "HTTP submission acknowledgement did not bind the submitted transaction");
      require(services.submission_last_timeout_ms() == 1'234U, "HTTP submission did not propagate its deadline");
      auto first_batch_item = protocol::transaction_submit_request{.timeout_ms = 1'000U};
      auto first_batch_transaction = protocol::signed_transaction{};
      first_batch_transaction.expiration = protocol::time_point_sec{1U};
      first_batch_item.transaction = protocol::packed_transaction{std::move(first_batch_transaction)};
      auto second_batch_item = protocol::transaction_submit_request{.timeout_ms = 2'000U};
      auto second_batch_transaction = protocol::signed_transaction{};
      second_batch_transaction.expiration = protocol::time_point_sec{2U};
      second_batch_item.transaction = protocol::packed_transaction{std::move(second_batch_transaction)};
      const auto batch_responses = forge::asio::blocking::run(
          runtime, submission.submit_batch(protocol::transaction_submit_batch_request{
                       .transactions = {std::move(first_batch_item), std::move(second_batch_item)},
                       .timeout_ms = 2'500U,
                   }));
      require(batch_responses.size() == 2U, "HTTP batch submission changed response cardinality");
      require(services.submission_last_batch_timeout_ms() == 2'500U,
              "HTTP batch submission did not propagate its total deadline");
      require_long_poll_transport(runtime, transaction_remote, services, "HTTP", true);
   } catch (...) {
      forge::asio::blocking::run(runtime, server.async_stop());
      throw;
   }
   forge::asio::blocking::run(runtime, server.async_stop());
   require(server.port() == 0, "HTTP chain API server remained bound after async_stop");
   return responses;
}

} // namespace package_chain_api_component
