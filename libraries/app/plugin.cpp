module;

#include <boost/asio/awaitable.hpp>

#include <optional>

module forge.app.plugin;

import forge.config.core.component;
import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;

namespace forge::app {

std::optional<config::core::component_descriptor> plugin::describe_config() const {
   return std::nullopt;
}

boost::asio::awaitable<void> plugin::configure(config::core::component_view) {
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::core::provider&) {
   co_return;
}

void plugin::request_stop() noexcept {}

bool valid_plugin_id(const plugin_id& id) noexcept {
   return !id.value.empty();
}

} // namespace forge::app
