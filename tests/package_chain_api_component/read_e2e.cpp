module;

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

module package.chain_api_component.read_e2e;

import package.chain_api_component.p2p_runtime;
import package.chain_api_component.read_fixture;
import forge.api.core.binding;
import forge.api.core.exceptions;
import forge.api.core.registry;
import forge.api.http.binding;
import forge.api.http.proxy;
import forge.app.application;
import forge.app.application_shell;
import forge.app.plugin_context;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.chain.api.block;
import forge.chain.api.exceptions;
import forge.chain.api.info;
import forge.chain.api.limits;
import forge.chain.api.state;
import forge.chain.protocol.audit;
import forge.chain.protocol.block_query;
import forge.chain.protocol.info;
import forge.chain.protocol.state_query;
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

struct read_responses {
   protocol::info_response information;
   protocol::block_state_response block;
   protocol::table_changes_response state;
   bool oversized_request_rejected = false;
};

read_responses run_http_read_e2e(read_services& services) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::core::registry{};
   apis.install<chain_api::info>(chain_api::limited_descriptor<chain_api::info>(package_limits()), services.information());
   apis.install<chain_api::block>(chain_api::limited_descriptor<chain_api::block>(package_limits()), services.blocks());
   apis.install<chain_api::state>(chain_api::limited_descriptor<chain_api::state>(package_limits()), services.state());

   auto router = forge::net::http::router{};
   router.mount(forge::api::http::binding().use(forge::api::core::binding().serve(apis).build()).bind<chain_api::info>().build());
   router.mount(forge::api::http::binding().use(forge::api::core::binding().serve(apis).build()).bind<chain_api::block>().build());
   router.mount(forge::api::http::binding().use(forge::api::core::binding().serve(apis).build()).bind<chain_api::state>().build());

   auto server = forge::net::http::server{
       runtime,
       forge::net::http::server_config{.max_request_body_bytes = chain_api_max_frame_size},
       std::move(router),
   };
   forge::asio::blocking::run(runtime, server.async_start());
   require(server.port() != 0, "HTTP chain API server did not bind");

   auto responses = read_responses{};
   try {
      auto client = forge::net::http::client{
          runtime,
          forge::net::http::parse_base_url("http://127.0.0.1:" + std::to_string(server.port())),
      };
      auto info_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::info>(client));
      auto block_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::block>(client));
      auto state_remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::state>(client));
      responses.information = forge::asio::blocking::run(
          runtime, info_remote->get(protocol::anchored_request{.audit = protocol::audit_mode::required}));
      responses.block = forge::asio::blocking::run(runtime, block_remote->get_block_state(protocol::block_request{
                                                               .num = 40,
                                                               .audit = protocol::audit_mode::required,
                                                           }));
      responses.state = forge::asio::blocking::run(runtime, state_remote->get_table_changes(protocol::table_changes_request{
                                                            .from_block = 39,
                                                            .to_block = 40,
                                                            .tables = {{.code = protocol::account_name{"tester"},
                                                                        .scope = protocol::name{"scope"},
                                                                        .table = protocol::name{"rows"}}},
                                                            .audit = protocol::audit_mode::required,
                                                        }));
      const auto calls_before_oversized = services.state_calls();
      try {
         static_cast<void>(forge::asio::blocking::run(runtime, state_remote->get_table_changes(protocol::table_changes_request{
                                                        .from_block = 39,
                                                        .to_block = 40,
                                                        .tables = {{.code = protocol::account_name{"tester"}}},
                                                        .cursor = protocol::bytes(70U * 1024U, 0x5aU),
                                                    })));
      } catch (const forge::chain::api::exceptions::resource_exhausted&) {
         responses.oversized_request_rejected = true;
      }
      require(responses.oversized_request_rejected, "HTTP chain API accepted an oversized typed-state request");
      require(services.state_calls() == calls_before_oversized, "HTTP oversized request reached the owner service");
   } catch (...) {
      forge::asio::blocking::run(runtime, server.async_stop());
      throw;
   }
   forge::asio::blocking::run(runtime, server.async_stop());
   require(server.port() == 0, "HTTP chain API server remained bound after async_stop");
   return responses;
}

read_responses run_p2p_read_e2e(read_services& services) {
   const auto server_peer = test_peer(0x41);
   auto server_config = p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::core::value::array_type{
                                              forge::config::core::value{"/ip4/127.0.0.1/udp/0/quic-v1"},
                                          });
   auto server = p2p_server_application{p2p_publication_callbacks{
       .install = [&services](forge::app::application_context& context) -> boost::asio::awaitable<void> {
          context.apis().install<chain_api::info>(chain_api::limited_descriptor<chain_api::info>(package_limits()),
                                                  services.information());
          context.apis().install<chain_api::block>(chain_api::limited_descriptor<chain_api::block>(package_limits()),
                                                   services.blocks());
          context.apis().install<chain_api::state>(chain_api::limited_descriptor<chain_api::state>(package_limits()),
                                                   services.state());
          co_return;
       },
       .binding = [](forge::app::plugin_context& context) {
          return forge::api::core::binding()
              .serve(context.apis())
              .export_api<chain_api::info>({.id = {"forge.chain.api.info"}, .major = 1, .min_revision = 0})
              .export_api<chain_api::block>({.id = {"forge.chain.api.block"}, .major = 1, .min_revision = 0})
              .export_api<chain_api::state>({.id = {"forge.chain.api.state"}, .major = 3, .min_revision = 0})
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

      auto client_config = p2p_config(test_peer(0x42));
      client_config.set("plugins.p2p.node.bootstrap", forge::config::core::value::array_type{
                                                    forge::config::core::value{server_endpoint->to_string()},
                                                });
      client.configure(client_config);
      forge::asio::blocking::run(client.runtime(), client.startup());
      client_started = true;

      auto resolver = client.apis().get<forge::plugins::p2p::resolver::api>(
          {.id = {"forge.plugins.p2p.resolver"}, .major = 2, .min_revision = 0});
      const auto remote_apis = forge::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer));
      require(remote_apis.size() == 3, "P2P resolver did not advertise every read chain API");
      require_advertised_api(remote_apis, "forge.chain.api.info");
      require_advertised_api(remote_apis, "forge.chain.api.block");
      require_advertised_api(remote_apis, "forge.chain.api.state");
      const auto resolution = forge::asio::blocking::run(
          client.runtime(), resolver->resolve(server_peer, {.id = {"forge.chain.api.info"}, .major = 1, .min_revision = 0}));
      require(resolution.api.protocol == chain_api_protocol, "P2P resolver selected the wrong chain API protocol");

      auto info_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::info>(server_peer));
      auto block_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::block>(server_peer));
      auto state_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::state>(server_peer));
      auto responses = read_responses{};
      responses.information = forge::asio::blocking::run(
          client.runtime(), info_remote->get(protocol::anchored_request{.audit = protocol::audit_mode::required}));
      responses.block = forge::asio::blocking::run(client.runtime(), block_remote->get_block_state(protocol::block_request{
                                                                      .num = 40,
                                                                      .audit = protocol::audit_mode::required,
                                                                  }));
      responses.state = forge::asio::blocking::run(client.runtime(), state_remote->get_table_changes(protocol::table_changes_request{
                                                                   .from_block = 39,
                                                                   .to_block = 40,
                                                                   .tables = {{.code = protocol::account_name{"tester"},
                                                                               .scope = protocol::name{"scope"},
                                                                               .table = protocol::name{"rows"}}},
                                                                   .audit = protocol::audit_mode::required,
                                                               }));
      const auto calls_before_oversized = services.state_calls();
      try {
         static_cast<void>(forge::asio::blocking::run(client.runtime(), state_remote->get_table_changes(
             protocol::table_changes_request{
                 .from_block = 39,
                 .to_block = 40,
                 .tables = {{.code = protocol::account_name{"tester"}}},
                 .cursor = protocol::bytes(70U * 1024U, 0x5aU),
             })));
      } catch (const forge::chain::api::exceptions::resource_exhausted&) {
         responses.oversized_request_rejected = true;
      }
      require(responses.oversized_request_rejected, "P2P chain API accepted an oversized typed-state request");
      require(services.state_calls() == calls_before_oversized, "P2P oversized request reached the owner service");

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

void run_read_e2e() {
   const auto expectations = make_read_expectations();
   auto services = make_read_services(expectations);
   const auto http = run_http_read_e2e(services);
   const auto p2p = run_p2p_read_e2e(services);

   require(http.information == expectations.information, "HTTP info API changed chain audit DTO semantics");
   require(http.information.head_time == expectations.information.head_time, "HTTP info API lost head time microseconds");
   require(http.information.finalized_time == expectations.information.finalized_time,
           "HTTP info API lost finalized time microseconds");
   require(http.block == expectations.block, "HTTP block API changed typed DTO semantics");
   require(http.state == expectations.state, "HTTP state API changed typed DTO semantics");
   require(http.oversized_request_rejected, "HTTP Chain API request limit was not exercised");
   require(p2p.information == expectations.information, "P2P info API changed chain audit DTO semantics");
   require(p2p.block == expectations.block, "P2P block API changed typed DTO semantics");
   require(p2p.state == expectations.state, "P2P state API changed chain audit DTO semantics");
   require(p2p.oversized_request_rejected, "P2P Chain API request limit was not exercised");
   require(http.information == p2p.information, "HTTP and P2P info API responses diverged");
   require(http.block == p2p.block, "HTTP and P2P block API responses diverged");
   require(http.state == p2p.state, "HTTP and P2P state API responses diverged");
   require(forge::raw::pack(http.information) == forge::raw::pack(p2p.information),
           "HTTP and P2P info canonical bytes diverged");
   require(forge::raw::pack(http.block) == forge::raw::pack(p2p.block), "HTTP and P2P block canonical bytes diverged");
   require(forge::raw::pack(http.state) == forge::raw::pack(p2p.state), "HTTP and P2P state canonical bytes diverged");
   require_audit_semantics(http.information);
   require_audit_semantics(http.block);
   require_audit_semantics(http.state, 1U);
   require_audit_semantics(p2p.information);
   require_audit_semantics(p2p.block);
   require_audit_semantics(p2p.state, 1U);
   require(services.information_calls() == 2, "info transport E2E did not dispatch both typed calls");
   require(services.block_calls() == 2, "transport E2E did not dispatch both block typed calls");
   require(services.state_calls() == 2, "state transport E2E did not dispatch both typed calls");
   require(services.information_audit_required(), "info transport E2E changed the requested audit mode");
   require(services.block_audit_required(), "P2P block API changed the requested audit mode");
   require(services.state_audit_required(), "P2P state API changed the requested audit mode");
}

} // namespace package_chain_api_component
