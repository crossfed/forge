#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "p2p_e2e.hxx"
#include "secrets_fixture.hxx"

import forge.api.core.binding;
import forge.api.core.exceptions;
import forge.api.p2p.publication;
import forge.api.transport.options;
import forge.app.application;
import forge.app.application_shell;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.plugin_registry;
import forge.asio.blocking;
import forge.asio.exceptions;
import forge.asio.runtime;
import forge.chain.api.admin;
import forge.chain.api.block;
import forge.chain.api.exceptions;
import forge.chain.api.info;
import forge.chain.api.limits;
import forge.chain.api.state;
import forge.chain.api.submission;
import forge.chain.api.submission_client;
import forge.chain.api.transaction;
import forge.chain.protocol.admin;
import forge.chain.protocol.block_query;
import forge.chain.protocol.info;
import forge.chain.protocol.state_query;
import forge.chain.protocol.transaction_query;
import forge.config.core.document;
import forge.config.core.value;
import forge.net.p2p.endpoint;
import forge.net.p2p.identity;
import forge.net.p2p.protocol;
import forge.plugins.crypto.secrets.api;
import forge.plugins.crypto.secrets.types;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.plugin;
import forge.plugins.p2p.resolver.api;
import forge.plugins.p2p.resolver.plugin;
import forge.plugins.p2p.resolver.types;
import forge.raw.raw;

namespace {

namespace chain_api = forge::chain::api;
namespace protocol = forge::chain::protocol;
namespace crypto_secrets = forge::plugins::crypto::secrets;

constexpr auto chain_api_protocol = std::string_view{"/spine/chain/api/1"};
constexpr auto chain_api_max_request_size = std::uint32_t{64U * 1024U};
constexpr auto chain_api_max_frame_size = std::uint32_t{512U * 1024U};

void require(bool condition, std::string_view message) {
   if (!condition) {
      throw std::runtime_error{std::string{message}};
   }
}

protocol::service_limits package_limits() {
   return {
       .max_page_size = 256,
       .max_state_batch_size = 32,
       .max_transaction_batch_size = 16,
       .max_container_elements = 1'024,
       .max_transaction_status_candidates = 512,
       .max_request_bytes = chain_api_max_request_size,
       .max_response_bytes = chain_api_max_request_size,
       .max_proof_bytes = 1U << 20U,
       .max_await_ms = 300'000,
       .state_retention_blocks = 512,
   };
}

class p2p_dependency : public forge::app::plugin {
 public:
   explicit p2p_dependency(std::string id) : id_{std::move(id)} {}

   [[nodiscard]] forge::app::plugin_id id() const override {
      return forge::app::plugin_id{.value = id_};
   }

   [[nodiscard]] std::string version() const override {
      return "test";
   }

   boost::asio::awaitable<void> initialize(forge::app::plugin_context&) override {
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }

 private:
   std::string id_;
};

class p2p_secrets_api final : public crypto_secrets::api {
 public:
   boost::asio::awaitable<crypto_secrets::snapshot> status(crypto_secrets::query) override {
      co_return crypto_secrets::snapshot{.configured_secrets = 2};
   }

   boost::asio::awaitable<crypto_secrets::get_result> get_bytes(crypto_secrets::get_request request) override {
      const auto* material = request.secret_id == "p2p/test-certificate"   ? &chain_api_test::certificate
                             : request.secret_id == "p2p/test-private-key" ? &chain_api_test::private_key
                                                                           : nullptr;
      if (material == nullptr) {
         throw std::runtime_error{"unknown P2P package-test secret"};
      }
      auto bytes = decltype(crypto_secrets::get_result{}.bytes){};
      bytes.reserve(material->size());
      for (const auto value : *material) {
         bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
      }
      co_return crypto_secrets::get_result{.secret_id = std::move(request.secret_id), .bytes = std::move(bytes)};
   }

   boost::asio::awaitable<crypto_secrets::derive_result> derive_hkdf_sha256(crypto_secrets::derive_request) override {
      throw std::logic_error{"P2P package-test secrets do not implement derivation"};
      co_return crypto_secrets::derive_result{};
   }

   boost::asio::awaitable<crypto_secrets::aead_encrypt_result>
   encrypt_aes_gcm(crypto_secrets::aead_encrypt_request) override {
      throw std::logic_error{"P2P package-test secrets do not implement encryption"};
      co_return crypto_secrets::aead_encrypt_result{};
   }

   boost::asio::awaitable<crypto_secrets::aead_decrypt_result>
   decrypt_aes_gcm(crypto_secrets::aead_decrypt_request) override {
      throw std::logic_error{"P2P package-test secrets do not implement decryption"};
      co_return crypto_secrets::aead_decrypt_result{};
   }
};

class p2p_secrets_plugin final : public p2p_dependency {
 public:
   p2p_secrets_plugin() : p2p_dependency{"forge.plugins.crypto.secrets"} {}

   boost::asio::awaitable<void> provide(forge::api::core::provider& provider) override {
      provider.install<crypto_secrets::api>(std::make_shared<p2p_secrets_api>());
      co_return;
   }
};

void register_p2p_stack(forge::app::plugin_registry& registry) {
   registry.register_plugin(forge::app::plugin_descriptor{
       .id = forge::app::plugin_id{.value = "forge.plugins.db.store"},
       .factory = [] { return std::make_unique<p2p_dependency>("forge.plugins.db.store"); },
   });
   registry.register_plugin(forge::app::plugin_descriptor{
       .id = forge::app::plugin_id{.value = "forge.plugins.crypto.secrets"},
       .factory = [] { return std::make_unique<p2p_secrets_plugin>(); },
   });
   registry.register_plugin(forge::plugins::p2p::node::descriptor());
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
                                 const p2p_services& services) {
   const auto deadlines_before = services.transaction_await_deadlines();
   auto deadline_observed = false;
   try {
      static_cast<void>(forge::asio::blocking::run(
          runtime, remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 10})));
   } catch (const chain_api::exceptions::deadline_exceeded&) {
      deadline_observed = true;
   }
   require(deadline_observed, "P2P long-poll ignored its request deadline");
   require(services.transaction_await_deadlines() == deadlines_before + 1U,
           "P2P deadline did not originate at the owner");

   const auto started_before = services.transaction_await_started();
   const auto cancellations_before = services.transaction_await_cancellations();
   auto cancellation = boost::asio::cancellation_signal{};
   auto pending = boost::asio::co_spawn(
       runtime.context(), remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 300'000}),
       boost::asio::bind_cancellation_slot(cancellation.slot(), boost::asio::use_future));
   wait_until([&] { return services.transaction_await_started() > started_before; },
              "P2P long-poll did not reach the owner");
   cancellation.emit(boost::asio::cancellation_type::all);
   auto caller_cancelled = false;
   try {
      static_cast<void>(pending.get());
   } catch (const forge::api::core::exceptions::cancelled&) {
      caller_cancelled = true;
   } catch (const forge::asio::exceptions::canceled&) {
      caller_cancelled = true;
   }
   require(caller_cancelled, "P2P long-poll did not return typed cancellation");
   wait_until([&] { return services.transaction_await_cancellations() > cancellations_before; },
              "P2P long-poll cancellation did not reach the owner");

   static_cast<void>(forge::asio::blocking::run(runtime, remote->get_status(protocol::transaction_status_request{})));
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
          {.id = {"forge.plugins.p2p.resolver"}, .major = 2, .min_revision = 0});
      auto plan = forge::api::core::binding()
                      .serve(context.apis())
                      .export_api<chain_api::info>({.id = {"forge.chain.api.info"}, .major = 1, .min_revision = 0})
                      .export_api<chain_api::block>({.id = {"forge.chain.api.block"}, .major = 1, .min_revision = 0})
                      .export_api<chain_api::state>({.id = {"forge.chain.api.state"}, .major = 2, .min_revision = 0})
                      .export_api<chain_api::transaction>(
                          {.id = {"forge.chain.api.transaction"}, .major = 1, .min_revision = 0})
                      .export_api<chain_api::submission>(
                          {.id = {"forge.chain.api.submission"}, .major = 1, .min_revision = 0})
                      .export_api<chain_api::admin>({.id = {"forge.chain.api.admin"}, .major = 1, .min_revision = 0})
                      .build();
      publication_ = resolver->publish_api(
          std::move(plan), forge::net::p2p::protocol_id{.value = std::string{chain_api_protocol}},
          forge::plugins::p2p::resolver::publish_options{
              .transport = forge::api::transport::options{.max_frame_size = chain_api_max_frame_size},
          });
      co_return;
   }

   boost::asio::awaitable<void> startup() override {
      co_return;
   }

   boost::asio::awaitable<void> shutdown() override {
      co_return;
   }

 private:
   forge::api::p2p::publication publication_;
};

class p2p_server_application final : public forge::app::application_shell {
 public:
   explicit p2p_server_application(p2p_services services) : services_{std::move(services)} {}

 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      register_p2p_stack(registry);
      registry.register_plugin(forge::plugins::p2p::resolver::descriptor());
      registry.register_plugin(forge::app::plugin_descriptor{
          .id = forge::app::plugin_id{.value = "chain-api-publisher"},
          .dependencies = {forge::app::plugin_id{.value = "forge.plugins.p2p.resolver"}},
          .factory = [] { return std::make_unique<chain_api_publisher>(); },
      });
   }

   boost::asio::awaitable<void> on_provide(forge::app::application_context& context) override {
      context.apis().install<chain_api::info>(chain_api::limited_descriptor<chain_api::info>(package_limits()),
                                              services_.information);
      context.apis().install<chain_api::block>(chain_api::limited_descriptor<chain_api::block>(package_limits()),
                                               services_.blocks);
      context.apis().install<chain_api::state>(chain_api::limited_descriptor<chain_api::state>(package_limits()),
                                               services_.state);
      context.apis().install<chain_api::transaction>(
          chain_api::limited_descriptor<chain_api::transaction>(package_limits()), services_.transactions);
      context.apis().install<chain_api::submission>(
          chain_api::limited_descriptor<chain_api::submission>(package_limits()), services_.submissions);
      context.apis().install<chain_api::admin>(chain_api::limited_descriptor<chain_api::admin>(package_limits()),
                                               services_.administration);
      co_return;
   }

 private:
   p2p_services services_;
};

class p2p_client_application final : public forge::app::application_shell {
 protected:
   void on_register_plugins(forge::app::plugin_registry& registry) override {
      register_p2p_stack(registry);
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
   config.set("plugins.p2p.node.identity.certificate-secret", "p2p/test-certificate");
   config.set("plugins.p2p.node.identity.private-key-secret", "p2p/test-private-key");
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

void require_advertised_api(const auto& apis, std::string_view id) {
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

} // namespace

p2p_responses run_p2p_e2e(const p2p_services& services) {
   const auto server_peer = test_peer(0x41);
   auto server_config = p2p_config(server_peer);
   server_config.set("plugins.p2p.node.listen", forge::config::core::value::array_type{
                                                    forge::config::core::value{
                                                        "/ip4/127.0.0.1/udp/0/quic-v1",
                                                    },
                                                });

   auto server = p2p_server_application{services};
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
      require(remote_apis.size() == 6, "P2P resolver did not advertise all six chain APIs");
      require_advertised_api(remote_apis, "forge.chain.api.info");
      require_advertised_api(remote_apis, "forge.chain.api.block");
      require_advertised_api(remote_apis, "forge.chain.api.state");
      require_advertised_api(remote_apis, "forge.chain.api.transaction");
      require_advertised_api(remote_apis, "forge.chain.api.submission");
      require_advertised_api(remote_apis, "forge.chain.api.admin");

      const auto resolution = forge::asio::blocking::run(
          client.runtime(),
          resolver->resolve(server_peer, {.id = {"forge.chain.api.info"}, .major = 1, .min_revision = 0}));
      require(resolution.api.protocol == chain_api_protocol, "P2P resolver selected the wrong chain API protocol");

      auto limits_remote =
          forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::transaction>(server_peer));
      const auto started_before = services.transaction_await_started();
      auto limit_rejected = false;
      try {
         static_cast<void>(forge::asio::blocking::run(
             client.runtime(),
             limits_remote->await_transaction(protocol::transaction_await_request{.timeout_ms = 300'001})));
      } catch (const chain_api::exceptions::resource_exhausted&) {
         limit_rejected = true;
      }
      require(limit_rejected, "P2P owner boundary accepted an oversized await deadline");
      require(services.transaction_await_started() == started_before,
              "P2P oversized await deadline reached the owner");

      auto info_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::info>(server_peer));
      auto block_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::block>(server_peer));
      auto state_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::state>(server_peer));
      auto transaction_remote =
          forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::transaction>(server_peer));
      auto submission_remote =
          forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::submission>(server_peer));
      auto admin_remote = forge::asio::blocking::run(client.runtime(), resolver->remote<chain_api::admin>(server_peer));

      auto responses = p2p_responses{};
      const auto information = forge::asio::blocking::run(
          client.runtime(), info_remote->get(protocol::anchored_request{.audit = protocol::audit_mode::required}));
      const auto block =
          forge::asio::blocking::run(client.runtime(), block_remote->get_block_state(protocol::block_request{
                                                           .num = 40,
                                                           .audit = protocol::audit_mode::required,
                                                       }));
      const auto state =
          forge::asio::blocking::run(client.runtime(), state_remote->get_table_changes(protocol::table_changes_request{
                                                           .from_block = 39,
                                                           .to_block = 40,
                                                           .tables = {{.code = protocol::account_name{"tester"},
                                                                       .scope = protocol::name{"scope"},
                                                                       .table = protocol::name{"rows"}}},
                                                           .audit = protocol::audit_mode::required,
                                                       }));
      const auto transaction = forge::asio::blocking::run(
          client.runtime(), transaction_remote->compute_transaction(protocol::transaction_read_only_request{
                                .audit = protocol::audit_mode::required,
                            }));
      auto submission = chain_api::submission_client{std::move(submission_remote)};
      auto submitted = protocol::transaction_submit_request{};
      submitted.transaction = protocol::packed_transaction{protocol::signed_transaction{}};
      const auto submitted_id = submitted.transaction.id();
      require(forge::asio::blocking::run(client.runtime(), submission.submit(std::move(submitted))).id == submitted_id,
              "P2P submission acknowledgement did not bind the submitted transaction");
      const auto administration =
          forge::asio::blocking::run(client.runtime(), admin_remote->producer_status(protocol::admin_query{}));
      require_long_poll_transport(client.runtime(), transaction_remote, services);

      const auto calls_before_oversized = services.state_calls();
      try {
         static_cast<void>(forge::asio::blocking::run(client.runtime(),
                                                      state_remote->get_table_changes(protocol::table_changes_request{
                                                          .from_block = 39,
                                                          .to_block = 40,
                                                          .tables = {{.code = protocol::account_name{"tester"}}},
                                                          .cursor = protocol::bytes(70U * 1024U, 0x5aU),
                                                      })));
      } catch (const chain_api::exceptions::resource_exhausted&) {
         responses.oversized_request_rejected = true;
      }
      require(responses.oversized_request_rejected, "P2P chain API accepted an oversized typed-state request");
      require(services.state_calls() == calls_before_oversized, "P2P oversized request reached the owner service");

      auto internal_error_preserved = false;
      try {
         static_cast<void>(forge::asio::blocking::run(
             client.runtime(), admin_remote->prune(protocol::prune_request{.through_block = 40, .max_records = 0})));
      } catch (const forge::api::core::exceptions::remote_internal&) {
         internal_error_preserved = true;
      }
      require(internal_error_preserved, "P2P chain API did not preserve remote error semantics");

      responses.information = forge::raw::pack(information);
      responses.block = forge::raw::pack(block);
      responses.state = forge::raw::pack(state);
      responses.transaction = forge::raw::pack(transaction);
      responses.administration = forge::raw::pack(administration);

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
      return responses;
   } catch (...) {
      shutdown_after_failure(client, client_started);
      shutdown_after_failure(server, server_started);
      throw;
   }
}
