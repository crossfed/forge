#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "p2p_identity.hpp"

import forge.api.core.binding;
import forge.api.core.registry;
import forge.api.core.types;
import forge.api.http.binding;
import forge.api.http.proxy;
import forge.app.application;
import forge.app.application_shell;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.plugin_registry;
import forge.asio.blocking;
import forge.asio.runtime;
import forge.chain.api.admin;
import forge.chain.api.block;
import forge.chain.api.info;
import forge.chain.api.raw_client;
import forge.chain.api.state;
import forge.chain.api.transaction;
import forge.chain.api.verified_client;
import forge.chain.protocol.admin;
import forge.chain.protocol.audit;
import forge.chain.protocol.block_query;
import forge.chain.protocol.info;
import forge.chain.protocol.state_query;
import forge.chain.protocol.transaction_query;
import forge.config.core.document;
import forge.config.core.value;
import forge.crypto.digest.sha256;
import forge.net.http.base_url;
import forge.net.http.client;
import forge.net.http.router;
import forge.net.http.server;
import forge.net.p2p.endpoint;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.plugin;
import forge.plugins.p2p.resolver.api;
import forge.plugins.p2p.resolver.plugin;
import forge.plugins.p2p.resolver.types;

namespace {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;

constexpr auto chain_api_protocol = std::string_view{"/spine/chain/api/1"};

void require(bool condition, std::string_view message) {
   if (!condition) {
      throw std::runtime_error{std::string{message}};
   }
}

protocol::digest hash(std::string_view value) {
   return forge::crypto::digest::sha256::hash(std::string{value});
}

protocol::info_response make_info_response() {
   const auto chain = hash("chain-api-e2e-chain");
   const auto head = hash("chain-api-e2e-head");
   const auto finalized = hash("chain-api-e2e-finalized");

   auto response = protocol::info_response{};
   response.context = protocol::response_context{
       .chain = chain,
       .head = head,
       .finalized = finalized,
       .anchor =
           protocol::state_anchor{
               .chain = chain,
               .block = finalized,
               .block_num = 40,
               .transaction_root = hash("chain-api-e2e-transactions"),
               .state_root = hash("chain-api-e2e-state"),
               .state_size = 17,
               .change_root = hash("chain-api-e2e-changes"),
               .change_count = 5,
           },
   };
   response.audit = protocol::audit_bundle{
       .finality =
           protocol::proof_blob{
               .scheme = "forge.chain.finality.test",
               .version = 3,
               .payload = {0x01, 0x02, 0x03},
           },
       .state =
           {
               protocol::proof_blob{
                   .scheme = "forge.db.authenticated.point",
                   .version = 2,
                   .payload = {0x10, 0x11, 0x12, 0x13},
               },
               protocol::proof_blob{
                   .scheme = "forge.db.authenticated.range",
                   .version = 4,
                   .payload = {0x20, 0x21, 0x22},
               },
           },
       .transaction =
           protocol::transaction_inclusion_proof{
               .leaf = hash("chain-api-e2e-transaction"),
               .index = 3,
               .leaf_count = 8,
               .path =
                   {
                       protocol::merkle_step{.sibling = hash("chain-api-e2e-sibling-left"), .sibling_on_left = true},
                       protocol::merkle_step{.sibling = hash("chain-api-e2e-sibling-right"), .sibling_on_left = false},
                   },
           },
   };
   response.chain = chain;
   response.server_version = "1.2.3";
   response.server_version_string = "forge-chain-api-e2e";
   response.server_full_version_string = "forge-chain-api-e2e+transport";
   response.head = head;
   response.head_num = 42;
   response.finalized = finalized;
   response.finalized_num = 40;
   response.best_candidate = hash("chain-api-e2e-candidate");
   response.best_candidate_num = 43;
   response.earliest_available_block_num = 7;
   response.virtual_block_cpu_limit = 1'000;
   response.virtual_block_net_limit = 2'000;
   response.block_cpu_limit = 900;
   response.block_net_limit = 1'800;
   response.total_cpu_weight = 77;
   response.total_net_weight = 88;
   response.available = protocol::capabilities{
       .methods =
           {
               protocol::method_capability{
                   .api = "forge.chain.api.info",
                   .method = "get",
                   .audit = protocol::audit_class::deterministic_composite,
                   .enabled = true,
               },
           },
       .http = true,
       .p2p = true,
       .archive = true,
   };
   response.limits = protocol::service_limits{
       .max_page_size = 256,
       .max_batch_size = 32,
       .max_proof_bytes = 1U << 20U,
       .retained_blocks = 512,
   };
   return response;
}

class info_implementation final : public chain_api::info {
 public:
   explicit info_implementation(protocol::info_response response) : response_{std::move(response)} {}

   boost::asio::awaitable<protocol::info_response> get(protocol::anchored_request request) override {
      last_audit.store(request.audit, std::memory_order_relaxed);
      calls.fetch_add(1, std::memory_order_relaxed);
      co_return response_;
   }

   std::atomic<std::uint32_t> calls{0};
   std::atomic<protocol::audit_mode> last_audit{protocol::audit_mode::none};

 private:
   protocol::info_response response_;
};

protocol::info_response run_http_e2e(const std::shared_ptr<info_implementation>& implementation) {
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto apis = forge::api::core::registry{};
   apis.install<chain_api::info>(chain_api::info::describe(), implementation);

   auto router = forge::net::http::router{};
   router.mount(forge::api::http::binding()
                    .use(forge::api::core::binding().serve(apis).build())
                    .bind<chain_api::info>()
                    .build());

   auto server = forge::net::http::server{runtime, forge::net::http::server_config{}, std::move(router)};
   forge::asio::blocking::run(runtime, server.async_start());
   require(server.port() != 0, "HTTP chain API server did not bind");

   auto response = protocol::info_response{};
   try {
      auto client = forge::net::http::client{
          runtime,
          forge::net::http::parse_base_url("http://127.0.0.1:" + std::to_string(server.port())),
      };
      auto remote = forge::asio::blocking::run(runtime, forge::api::http::remote<chain_api::info>(client));
      response = forge::asio::blocking::run(
          runtime, remote->get(protocol::anchored_request{.audit = protocol::audit_mode::required}));
   } catch (...) {
      forge::asio::blocking::run(runtime, server.async_stop());
      throw;
   }

   forge::asio::blocking::run(runtime, server.async_stop());
   require(server.port() == 0, "HTTP chain API server remained bound after async_stop");
   return response;
}

class chain_api_publisher final : public forge::app::plugin {
 public:
   forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = "chain-api-publisher"};
   }

   std::string version() const override {
      return "1";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override {
      auto resolver = context.apis().get<forge::plugins::p2p::resolver::api>(
          {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
      auto plan = forge::api::core::binding()
                      .serve(context.apis())
                      .export_api<chain_api::info>({.id = {"forge.chain.api.info"}, .major = 1, .min_revision = 0})
                      .build();
      resolver->publish_api(std::move(plan), forge::net::p2p::protocol_id{.value = std::string{chain_api_protocol}});
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }
};

class p2p_server_application final : public forge::app::application_shell {
 public:
   explicit p2p_server_application(std::shared_ptr<info_implementation> implementation)
       : implementation_{std::move(implementation)} {}

 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::plugins::p2p::resolver::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
          .id = forge::app::plugin_id{.value = "chain-api-publisher"},
          .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.resolver"}},
          .factory = [] { return std::make_unique<chain_api_publisher>(); },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<chain_api::info>(chain_api::info::describe(), implementation_);
      co_return;
   }

 private:
   std::shared_ptr<info_implementation> implementation_;
};

class p2p_client_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      registry.register_plugin(forge::plugins::p2p::node::descriptor());
      registry.register_plugin(forge::plugins::p2p::resolver::descriptor());
   }
};

forge::net::p2p::peer_id test_peer(std::uint8_t seed) {
   return forge::net::p2p::make_peer_id(
       {.type = forge::net::p2p::public_key::type::ed25519, .data = std::vector<std::uint8_t>(32, seed)});
}

forge::config::core::document p2p_config(const forge::net::p2p::peer_id& peer) {
   auto config = forge::config::core::document{};
   config.set("plugins.p2p.node.allow-insecure-test-mode", true);
   config.set("plugins.p2p.node.certificate-pem", std::string{chain_api_test::certificate});
   config.set("plugins.p2p.node.private-key-pem", std::string{chain_api_test::private_key});
   config.set("plugins.p2p.node.peer-id", peer.to_string());
   return config;
}

void shutdown_after_failure(forge::app::application_shell& application, bool started) noexcept {
   if (!started) {
      return;
   }
   application.request_stop();
   try {
      forge::asio::blocking::run(application.runtime(), application.shutdown());
   } catch (...) {
   }
}

protocol::info_response run_p2p_e2e(const std::shared_ptr<info_implementation>& implementation) {
   const auto server_peer = test_peer(0x41);
   auto server_config = p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::core::value::array_type{
                                                    forge::config::core::value{
                                                        "/ip4/127.0.0.1/udp/0/quic-v1",
                                                    },
                                                });

   auto server = p2p_server_application{implementation};
   auto client = p2p_client_application{};
   auto server_started = false;
   auto client_started = false;

   try {
      server.configure(server_config);
      forge::asio::blocking::run(server.runtime(), server.startup());
      server_started = true;

      auto server_node = server.apis().get<forge::plugins::p2p::node::api>(
          {.id = {"forge.plugins.p2p.node"}, .major = 1, .min_revision = 0});
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
          {.id = {"forge.plugins.p2p.resolver"}, .major = 1, .min_revision = 0});
      const auto remote_apis = forge::asio::blocking::run(client.runtime(), resolver->peer_apis(server_peer));
      require(remote_apis.size() == 1, "P2P resolver did not advertise exactly one chain API");
      require(remote_apis.front().id.value == "forge.chain.api.info", "P2P resolver advertised the wrong API");
      require(remote_apis.front().protocol == chain_api_protocol, "P2P resolver advertised the wrong protocol");

      const auto resolution = forge::asio::blocking::run(
          client.runtime(),
          resolver->resolve(server_peer, {.id = {"forge.chain.api.info"}, .major = 1, .min_revision = 0}));
      require(resolution.api.protocol == chain_api_protocol, "P2P resolver selected the wrong chain API protocol");

      auto remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::info>(server_peer));
      auto response = forge::asio::blocking::run(
          client.runtime(), remote->get(protocol::anchored_request{.audit = protocol::audit_mode::required}));

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
      require(server.state() == forge::app::application_state::stopped,
              "P2P chain API server did not reach stopped state");
      return response;
   } catch (...) {
      shutdown_after_failure(client, client_started);
      shutdown_after_failure(server, server_started);
      throw;
   }
}

void require_audit_semantics(const protocol::info_response& response) {
   require(response.context.anchor.has_value(), "transport dropped the audit anchor");
   require(response.audit.has_value(), "transport dropped the audit bundle");
   require(response.audit->finality.has_value(), "transport dropped the finality proof");
   require(response.audit->finality->payload == protocol::bytes{0x01, 0x02, 0x03},
           "transport changed finality proof bytes");
   require(response.audit->state.size() == 2, "transport changed state proof cardinality");
   require(response.audit->state[1].scheme == "forge.db.authenticated.range",
           "transport changed ranked range proof metadata");
   require(response.audit->transaction.has_value(), "transport dropped transaction inclusion proof");
   require(response.audit->transaction->path.size() == 2, "transport changed transaction proof path");
}

} // namespace

int main() {
   static_assert(std::is_abstract_v<forge::chain::api::info>);
   static_assert(std::is_abstract_v<forge::chain::api::block>);
   static_assert(std::is_abstract_v<forge::chain::api::state>);
   static_assert(std::is_abstract_v<forge::chain::api::transaction>);
   static_assert(std::is_abstract_v<forge::chain::api::admin>);
   static_assert(std::is_same_v<decltype(protocol::table_scope_request{}.cursor), std::optional<protocol::bytes>>);
   static_assert(std::is_same_v<decltype(protocol::table_scope_response{}.next), std::optional<protocol::bytes>>);

   auto request = protocol::state_point_request{};
   auto block = protocol::block_request{};
   auto transaction = protocol::transaction_status_request{};
   (void)request;
   (void)block;
   (void)transaction;

   const auto expected = make_info_response();
   auto implementation = std::make_shared<info_implementation>(expected);
   const auto http_response = run_http_e2e(implementation);
   const auto p2p_response = run_p2p_e2e(implementation);

   require(http_response == expected, "HTTP binding changed chain audit DTO semantics");
   require(p2p_response == expected, "P2P remote changed chain audit DTO semantics");
   require(http_response == p2p_response, "HTTP and P2P chain API responses diverged");
   require_audit_semantics(http_response);
   require_audit_semantics(p2p_response);
   require(implementation->calls.load(std::memory_order_relaxed) == 2,
           "transport E2E did not dispatch both typed calls");
   require(implementation->last_audit.load(std::memory_order_relaxed) == protocol::audit_mode::required,
           "transport E2E changed the requested audit mode");
   return 0;
}
