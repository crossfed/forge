module;

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>

#include <chrono>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

module package.chain_api_component.write_e2e;

import package.chain_api_component.p2p_runtime;
import package.chain_api_component.write_fixture;
import forge.api.core.binding;
import forge.api.core.exceptions;
import forge.api.core.handle;
import forge.api.core.registry;
import forge.api.http.binding;
import forge.api.http.proxy;
import forge.app.application;
import forge.app.application_shell;
import forge.app.plugin_context;
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
import forge.chain.protocol.audit;
import forge.chain.protocol.transaction_query;
import forge.config.core.value;
import forge.net.http.base_url;
import forge.net.http.client;
import forge.net.http.router;
import forge.net.http.server;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.resolver.api;
import forge.raw.raw;

namespace package_chain_api_component {

namespace {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

template <typename Apis> void require_advertised_api(const Apis& apis, std::string_view id) {
   for (const auto& api : apis) {
      if (api.id.value == id) {
         require(api.protocol == chain_api_protocol, "P2P resolver advertised a chain API on the wrong protocol");
         require(api.max_frame_size == chain_api_max_frame_size,
                 "P2P resolver advertised the wrong chain API frame limit");
         return;
      }
   }
   throw std::runtime_error{"P2P resolver omitted " + std::string{id}};
}

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

struct write_responses {
   protocol::transaction_read_only_response transaction;
   protocol::producer_status_response administration;
   bool internal_error_preserved = false;
};

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
      static_cast<void>(forge::asio::blocking::run(
          runtime, submission_limits_remote->submit(protocol::transaction_submit_request{
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
   router.mount(forge::api::http::binding().use(forge::api::core::binding().serve(apis).build()).bind<chain_api::transaction>().build());
   router.mount(forge::api::http::binding().use(forge::api::core::binding().serve(apis).build()).bind<chain_api::submission>().build());
   router.mount(forge::api::http::binding().use(forge::api::core::binding().serve(apis).build()).bind<chain_api::admin>().build());
   auto server = forge::net::http::server{
       runtime,
       forge::net::http::server_config{.max_request_body_bytes = chain_api_max_frame_size},
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
      responses.administration = forge::asio::blocking::run(runtime, admin_remote->producer_status(protocol::admin_query{}));
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

write_responses run_p2p_write_e2e(write_services& services) {
   const auto server_peer = test_peer(0x51);
   auto server_config = p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::core::value::array_type{
                                              forge::config::core::value{"/ip4/127.0.0.1/udp/0/quic-v1"},
                                          });
   auto server = p2p_server_application{p2p_publication_callbacks{
       .install = [&services](forge::app::application_context& context) -> boost::asio::awaitable<void> {
          context.apis().install<chain_api::transaction>(chain_api::limited_descriptor<chain_api::transaction>(package_limits()),
                                                         services.transactions());
          context.apis().install<chain_api::submission>(chain_api::limited_descriptor<chain_api::submission>(package_limits()),
                                                        services.submissions());
          context.apis().install<chain_api::admin>(chain_api::limited_descriptor<chain_api::admin>(package_limits()),
                                                   services.administration());
          co_return;
       },
       .binding = [](forge::app::plugin_context& context) {
          return forge::api::core::binding()
              .serve(context.apis())
              .export_api<chain_api::transaction>({.id = {"forge.chain.api.transaction"}, .major = 1, .min_revision = 0})
              .export_api<chain_api::submission>({.id = {"forge.chain.api.submission"}, .major = 1, .min_revision = 0})
              .export_api<chain_api::admin>({.id = {"forge.chain.api.admin"}, .major = 1, .min_revision = 0})
              .build();
       },
   }};
   auto client = p2p_client_application{};
   auto server_started = false;
   auto client_started = false;
   try {
      server.configure(server_config);
      forge::asio::blocking::run(server.runtime(), server.startup());
      server_started = true;
      auto server_node = server.apis().get<forge::plugins::p2p::node::api>(
          {.id = {"forge.plugins.p2p.node"}, .major = 2, .min_revision = 0});
      const auto server_endpoint = server_node->local_endpoint();
      require(server_endpoint.has_value(), "P2P chain API server did not publish a local endpoint");
      auto client_config = p2p_config(test_peer(0x52));
      client_config.set("plugins.p2p.node.bootstrap", forge::config::core::value::array_type{
                                                    forge::config::core::value{server_endpoint->to_string()},
                                                });
      client.configure(client_config);
      forge::asio::blocking::run(client.runtime(), client.startup());
      client_started = true;
      auto resolver = client.apis().get<forge::plugins::p2p::resolver::api>(
          {.id = {"forge.plugins.p2p.resolver"}, .major = 2, .min_revision = 0});
      const auto remote_apis = forge::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer));
      require(remote_apis.size() == 3, "P2P resolver did not advertise every write chain API");
      require_advertised_api(remote_apis, "forge.chain.api.transaction");
      require_advertised_api(remote_apis, "forge.chain.api.submission");
      require_advertised_api(remote_apis, "forge.chain.api.admin");
      const auto resolution = forge::asio::blocking::run(
          client.runtime(), resolver->resolve(server_peer, {.id = {"forge.chain.api.transaction"}, .major = 1, .min_revision = 0}));
      require(resolution.api.protocol == chain_api_protocol, "P2P resolver selected the wrong chain API protocol");
      auto transaction_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::transaction>(server_peer));
      auto submission_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::submission>(server_peer));
      auto admin_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::admin>(server_peer));
      const auto started_before = services.transaction_await_started();
      auto limit_rejected = false;
      try {
         static_cast<void>(forge::asio::blocking::run(
             client.runtime(), transaction_remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 300'001})));
      } catch (const forge::chain::api::exceptions::resource_exhausted&) {
         limit_rejected = true;
      }
      require(limit_rejected, "P2P owner boundary accepted an oversized await deadline");
      require(services.transaction_await_started() == started_before, "P2P oversized await deadline reached the owner");
      auto responses = write_responses{};
      responses.transaction = forge::asio::blocking::run(
          client.runtime(), transaction_remote->compute_transaction(protocol::transaction_read_only_request{
                                .audit = protocol::audit_mode::required,
                            }));
      auto submission = chain_api::submission_client{std::move(submission_remote)};
      auto submitted = protocol::transaction_submit_request{};
      submitted.transaction = protocol::packed_transaction{protocol::signed_transaction{}};
      const auto submitted_id = submitted.transaction.id();
      require(forge::asio::blocking::run(client.runtime(), submission.submit(std::move(submitted))).id == submitted_id,
              "P2P submission acknowledgement did not bind the submitted transaction");
      responses.administration = forge::asio::blocking::run(client.runtime(), admin_remote->producer_status(protocol::admin_query{}));
      require_long_poll_transport(client.runtime(), transaction_remote, services, "P2P", true);
      try {
         static_cast<void>(forge::asio::blocking::run(
             client.runtime(), admin_remote->prune(protocol::prune_request{.through_block = 40, .max_records = 0})));
      } catch (const forge::api::core::exceptions::remote_internal&) {
         responses.internal_error_preserved = true;
      }
      require(responses.internal_error_preserved, "P2P chain API did not preserve remote error semantics");
      auto stop_thread = std::thread{[&client] { client.request_stop(); }};
      stop_thread.join();
      const auto shutdown_started = std::chrono::steady_clock::now();
      forge::asio::blocking::run(client.runtime(), client.shutdown());
      const auto shutdown_elapsed = std::chrono::steady_clock::now() - shutdown_started;
      client_started = false;
      require(shutdown_elapsed < std::chrono::seconds{2}, "P2P resolver client shutdown did not cancel promptly");
      require(client.state() == forge::app::application_state::stopped,
              "P2P resolver client did not reach stopped state");
      server.request_stop();
      forge::asio::blocking::run(server.runtime(), server.shutdown());
      server_started = false;
      require(server.state() == forge::app::application_state::stopped, "P2P chain API server did not reach stopped state");
      return responses;
   } catch (...) {
      shutdown_after_failure(client, client_started);
      shutdown_after_failure(server, server_started);
      throw;
   }
}

} // namespace

void run_write_e2e() {
   const auto expectations = make_write_expectations();
   auto services = make_write_services(expectations);
   const auto http = run_http_write_e2e(services);
   const auto p2p = run_p2p_write_e2e(services);
   require(http.transaction == expectations.transaction, "HTTP transaction API changed typed DTO semantics");
   require(http.administration == expectations.administration, "HTTP admin API changed typed DTO semantics");
   require(p2p.transaction == expectations.transaction, "P2P transaction API changed typed DTO semantics");
   require(p2p.administration == expectations.administration, "P2P admin API changed typed DTO semantics");
   require(p2p.internal_error_preserved, "P2P admin error semantics were not exercised");
   require(http.transaction == p2p.transaction, "HTTP and P2P transaction API responses diverged");
   require(http.administration == p2p.administration, "HTTP and P2P admin API responses diverged");
   require(forge::raw::pack(http.transaction) == forge::raw::pack(p2p.transaction),
           "HTTP and P2P transaction canonical bytes diverged");
   require(forge::raw::pack(http.administration) == forge::raw::pack(p2p.administration),
           "HTTP and P2P admin canonical bytes diverged");
   require_audit_semantics(http.transaction);
   require_audit_semantics(p2p.transaction);
   require(services.transaction_calls() == 2, "transport E2E did not dispatch both transaction typed calls");
   require(services.administration_calls() == 2, "transport E2E did not dispatch both admin typed calls");
   require(services.administration_error_calls() == 1, "P2P admin API did not dispatch its typed error call");
   require(services.transaction_audit_required(), "P2P transaction API changed the requested audit mode");
}

} // namespace package_chain_api_component
