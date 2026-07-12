module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
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
import forge.config.core.component;
import forge.config.core.decode;
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

#include "details/config.hxx"
#include "details/diagnostics_source.hxx"
#include "details/api_impl.hxx"
#include "details/plugin_impl.hxx"
#include "details/pubsub_source.hxx"

namespace forge::plugins::p2p::node {

plugin::plugin() : impl_{std::make_shared<impl>()} {}
plugin::~plugin() = default;

forge::app::plugin_id plugin::id() const {
   return forge::app::plugin_id{.value = "forge.plugins.p2p.node"};
}

std::string plugin::version() const {
   return "1.0.0";
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
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   auto& node = impl_->ensure_node();
   for (auto& route : impl_->routes) {
      node.register_protocol_handler(route.first, route.second);
   }
   for (const auto& endpoint : impl_->listen) {
      co_await node.async_listen(endpoint);
   }
   for (const auto& endpoint : impl_->bootstrap) {
      try {
         (void)co_await node.async_connect(endpoint);
      } catch (...) {
         forge::exceptions::capture_and_log("P2P bootstrap connect failed");
      }
   }
   impl_->started = true;
}

void plugin::request_stop() noexcept {
   impl_->stopping = true;
   if (impl_->raw) {
      impl_->raw->stop();
   }
}

boost::asio::awaitable<void> plugin::shutdown() {
   request_stop();
   if (impl_->node) {
      co_await impl_->node->async_stop();
      impl_->node.reset();
      impl_->raw = nullptr;
   }
   impl_->started = false;
}

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
      .id = forge::app::plugin_id{.value = "forge.plugins.p2p.node"},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace forge::plugins::p2p::node
