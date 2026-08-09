module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.plugins.p2p.node.plugin;

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;
import forge.api.transport.exceptions;
import forge.api.transport.options;
import forge.api.transport.client;
import forge.api.transport.connection;
import forge.api.transport.server;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.asio.runtime;
import forge.asio.task;
import forge.config.core.component;
import forge.config.core.decode;
import forge.crypto.core.types;
import forge.crypto.core.secret_bytes;
import forge.exceptions;
import forge.net.p2p.exceptions;
import forge.net.p2p.identity;
import forge.net.p2p.endpoint;
import forge.net.p2p.envelope;
import forge.net.p2p.identify;
import forge.net.p2p.diagnostics;
import forge.net.p2p.discovery;
import forge.net.p2p.dht;
import forge.net.p2p.rendezvous;
import forge.net.p2p.pubsub;
import forge.net.p2p.reachability;
import forge.net.p2p.hole_punch;
import forge.net.p2p.protocol;
import forge.net.p2p.message;
import forge.net.p2p.scoring;
import forge.net.p2p.relay;
import forge.net.p2p.resource_manager;
import forge.net.p2p.stream;
import forge.net.p2p.negotiation;
import forge.net.p2p.peer_store;
import forge.net.p2p.node;
import forge.api.p2p.binding;
import forge.plugins.p2p.node.api;
import forge.plugins.p2p.node.exceptions;
import forge.plugins.p2p.node.types;
import forge.plugins.crypto.secrets.api;
import forge.plugins.db.store.api;

#include "details/config.hxx"
#include "details/diagnostics_source.hxx"
#include "details/api_impl.hxx"
#include "details/object_peer_state_adapter.hxx"
#include "details/plugin_impl.hxx"
#include "details/pubsub_source.hxx"

namespace forge::plugins::p2p::node {
namespace {

[[nodiscard]] std::string secret_text(forge::crypto::core::bytes& bytes) {
   auto out = std::string{bytes.begin(), bytes.end()};
   forge::crypto::core::secure_erase(bytes);
   return out;
}

void clear_bytes(forge::crypto::core::bytes& bytes) noexcept {
   forge::crypto::core::secure_erase(bytes);
}

void clear_text(std::string& value) noexcept {
   forge::crypto::core::secure_erase(value);
}

} // namespace

plugin::plugin() : impl_{std::make_shared<impl>()} {}
plugin::~plugin() = default;

forge::app::plugin_id plugin::id() const {
   return forge::app::plugin_id{.value = "forge.plugins.p2p.node"};
}

std::string plugin::version() const {
   return "2.0.0";
}

std::optional<forge::config::core::component_descriptor> plugin::describe_config() const {
   return forge::config::core::describe_component<config>("plugins.p2p.node");
}

boost::asio::awaitable<void> plugin::configure(forge::config::core::component_view view) {
   const auto config = decode_config(view);
   apply_config(*impl_, config);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::core::provider& provider) {
   provider.install<api>(std::make_shared<api_impl>(impl_));
   provider.install<diagnostics_source>(std::make_shared<diagnostics_source_adapter>(impl_));
   provider.install<pubsub_source>(std::make_shared<pubsub_source_adapter>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context& context) {
   impl_->runtime = &context.scheduler().runtime_context();
   impl_->scheduler = &context.scheduler();
   if (!impl_->peer_store_name.empty()) {
      impl_->stores =
          context.apis()
              .get<forge::plugins::db::store::api>({.id = {"forge.plugins.db.store"}, .major = 2, .min_revision = 0})
              .operator->();
   }
   if (!impl_->certificate_secret.empty()) {
      impl_->secrets = context.apis()
                           .get<forge::plugins::crypto::secrets::api>(
                               {.id = {"forge.plugins.crypto.secrets"}, .major = 1, .min_revision = 0})
                           .operator->();
   }
   impl_->stopping.store(false, std::memory_order_release);
   co_return;
}

boost::asio::awaitable<void> plugin::after_initialize() {
   if (impl_->peer_store_name.empty()) {
      if (!impl_->options.allow_insecure_test_mode) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "P2P peer store is not configured");
      }
      co_return;
   }
   impl_->peer_state_store =
       std::make_shared<forge::plugins::db::store::store_handle>(co_await impl_->stores->store(impl_->peer_store_name));
   object_peer_state_adapter::register_schema(*impl_->peer_state_store);
}

boost::asio::awaitable<void> plugin::startup() {
   if (!impl_->certificate_secret.empty()) {
      auto certificate = co_await impl_->secrets->get_bytes(
          {.secret_id = impl_->certificate_secret, .purpose = "p2p.identity.certificate"});
      try {
         auto private_key = co_await impl_->secrets->get_bytes(
             {.secret_id = impl_->private_key_secret, .purpose = "p2p.identity.private-key"});
         try {
            impl_->options.certificate_pem = secret_text(certificate.bytes);
            impl_->options.private_key_pem = secret_text(private_key.bytes);
         } catch (...) {
            clear_bytes(private_key.bytes);
            clear_text(impl_->options.certificate_pem);
            impl_->options.private_key_pem.clear();
            throw;
         }
      } catch (...) {
         clear_bytes(certificate.bytes);
         throw;
      }
   }
   if (impl_->peer_state_store) {
      impl_->peer_state = co_await object_peer_state_adapter::async_open(impl_->stores, *impl_->peer_state_store);
      impl_->options.peer_state.persistence = impl_->peer_state;
   }

   auto& node = impl_->ensure_node();
   impl_->options.private_key_pem.clear();
   for (auto& route : impl_->routes) {
      node.register_protocol_handler(route.first, route.second);
   }
   co_await node.async_hydrate_peer_state();
   for (const auto& endpoint : impl_->listen) {
      co_await node.async_listen(endpoint);
   }
   co_await impl_->refresh_bootstrap();
   (void)co_await node.async_refresh_discovery();
   impl_->started = true;
   impl_->start_maintenance();
}

void plugin::request_stop() noexcept {
   impl_->request_maintenance_stop();
   if (impl_->node) {
      try {
         impl_->node->stop();
      } catch (...) {
         // shutdown() retries and reports a synchronous initiation failure.
      }
   }
}

boost::asio::awaitable<void> plugin::shutdown() {
   request_stop();
   auto failure = std::exception_ptr{};
   auto peer_state_closed = !impl_->node && !impl_->peer_state;
   try {
      co_await impl_->stop_maintenance();
   } catch (...) {
      failure = std::current_exception();
   }
   if (impl_->node) {
      try {
         co_await impl_->node->async_stop();
         peer_state_closed = true;
      } catch (...) {
         if (!failure) {
            failure = std::current_exception();
         }
      }
   } else if (impl_->peer_state) {
      try {
         co_await impl_->peer_state->async_close();
         peer_state_closed = true;
      } catch (...) {
         if (!failure) {
            failure = std::current_exception();
         }
      }
   }
   if (peer_state_closed) {
      impl_->node.reset();
      impl_->peer_state.reset();
      impl_->options.peer_state.persistence.reset();
      impl_->peer_state_store.reset();
      clear_text(impl_->options.certificate_pem);
      impl_->options.private_key_pem.clear();
      impl_->stores = nullptr;
      impl_->secrets = nullptr;
   }
   impl_->started = false;
   if (failure) {
      std::rethrow_exception(failure);
   }
}

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
       .id = forge::app::plugin_id{.value = "forge.plugins.p2p.node"},
       .dependencies =
           {
               forge::app::plugin_id{.value = "forge.plugins.db.store"},
               forge::app::plugin_id{.value = "forge.plugins.crypto.secrets"},
           },
       .factory = [] { return std::make_unique<plugin>(); },
   };
}

} // namespace forge::plugins::p2p::node
