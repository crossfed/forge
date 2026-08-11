module;

#include <optional>
#include <string>
#include <utility>

module forge.app.plugin_context;

import forge.asio.exceptions;

namespace forge::app {
namespace {

forge::api::core::registry& default_api_registry() {
   static auto registry = forge::api::core::registry{};
   return registry;
}

} // namespace

plugin_context::plugin_context(forge::asio::task::scheduler& scheduler, forge::api::core::registry& apis,
                               service_view services, signal_bus& signals, event_bus& events,
                               diagnostics_store* diagnostics, config_view config,
                               forge::asio::compute::executor compute)
    : scheduler_{&scheduler}, compute_{std::move(compute)}, apis_{&apis}, services_{services}, signals_{&signals},
      events_{&events}, diagnostics_{diagnostics}, config_{std::move(config)} {}

plugin_context::plugin_context(forge::asio::task::scheduler& scheduler, forge::api::core::registry& apis,
                               signal_bus& signals, event_bus& events, diagnostics_store* diagnostics,
                               config_view config, forge::asio::compute::executor compute)
    : plugin_context{scheduler, apis, service_view{}, signals, events, diagnostics, std::move(config),
                     std::move(compute)} {}

plugin_context::plugin_context(forge::asio::task::scheduler& scheduler, signal_bus& signals, event_bus& events,
                               diagnostics_store* diagnostics, config_view config,
                               forge::asio::compute::executor compute)
    : plugin_context{scheduler, default_api_registry(), signals, events, diagnostics, std::move(config),
                     std::move(compute)} {}

forge::asio::task::scheduler& plugin_context::scheduler() noexcept {
   return *scheduler_;
}

bool plugin_context::has_compute() const noexcept {
   return compute_.valid();
}

forge::asio::compute::executor plugin_context::compute() const {
   if (!has_compute()) {
      throw forge::asio::exceptions::invalid_state{"application compute pool is not configured"};
   }
   return compute_;
}

forge::api::core::view plugin_context::apis() const noexcept {
   return forge::api::core::view{*apis_};
}

service_view plugin_context::services() const noexcept {
   return services_;
}

signal_bus& plugin_context::signals() noexcept {
   return *signals_;
}

event_bus& plugin_context::events() noexcept {
   return *events_;
}

diagnostics_store* plugin_context::diagnostics() noexcept {
   return diagnostics_;
}

const config_view& plugin_context::config() const noexcept {
   return config_;
}

std::optional<std::string> plugin_context::config_value(const std::string& key) const {
   const auto iterator = config_.find(key);
   if (iterator == config_.end()) {
      return std::nullopt;
   }
   return iterator->second;
}

} // namespace forge::app
