module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <coroutine>
#include <exception>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module forge.plugins.crypto.signer.plugin;

import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.app.plugin_registry;
import forge.config.core.component;
import forge.config.core.decode;
import forge.config.core.document;
import forge.config.core.value;
import forge.crypto.asymmetric;
import forge.crypto.sha256;
import forge.exceptions;
import forge.plugins.crypto.signer.api;
import forge.plugins.crypto.signer.exceptions;
import forge.plugins.crypto.signer.types;
import forge.schema.diagnostic;
import forge.schema.value_kind;
import forge.schema.object;
import forge.schema.enums;

#include "details/config.hxx"
#include "details/plugin_impl.hxx"
#include "details/api_impl.hxx"

namespace forge::plugins::crypto::signer {

plugin::plugin(plugin_options value) : impl_{std::make_shared<impl>(std::move(value))} {}

plugin::~plugin() = default;

forge::app::plugin_descriptor descriptor(plugin_options value) {
   return forge::app::plugin_descriptor{
      .id = {.value = "forge.plugins.crypto.signer"},
      .factory = [value = std::move(value)] { return std::make_unique<plugin>(value); },
   };
}

forge::app::plugin_id plugin::id() const {
   return {.value = "forge.plugins.crypto.signer"};
}

std::string plugin::version() const {
   return "2.0.0";
}

std::optional<forge::config::core::component_descriptor> plugin::describe_config() const {
   auto descriptor = forge::config::core::describe_component<config>("plugins.crypto.signer");
   descriptor.fields.push_back(forge::config::core::field_descriptor{
      .name = "default-output-profile",
      .kind = forge::schema::value_kind::string,
      .deprecated = true,
      .deprecated_message = "removed in Forge 8.9; format typed signer results at the consumer boundary",
      .description = "Removed migration tombstone; supplied values are rejected",
   });
   return descriptor;
}

boost::asio::awaitable<void> plugin::configure(forge::config::core::component_view view) {
   apply_config(*impl_, view);
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::core::provider& provider) {
   provider.install<api>(std::make_shared<api_impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context&) {
   impl_->stopping = false;
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   co_return;
}

void plugin::request_stop() noexcept {
   impl_->stopping = true;
}

boost::asio::awaitable<void> plugin::shutdown() {
   impl_->stopping = true;
   co_return;
}

} // namespace forge::plugins::crypto::signer
