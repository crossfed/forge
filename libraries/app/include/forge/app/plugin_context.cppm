module;

#include <map>
#include <optional>
#include <string>

export module forge.app.plugin_context;

import forge.app.diagnostics;
import forge.app.events;
export import forge.app.service_registry;
import forge.app.signals;
import forge.asio.compute;
import forge.asio.task;
import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;

export namespace forge::app {

using config_view = std::map<std::string, std::string>;

class plugin_context {
 public:
   plugin_context(forge::asio::task::scheduler& scheduler, forge::api::core::registry& apis,
                  service_view services, signal_bus& signals, event_bus& events,
                  diagnostics_store* diagnostics = nullptr, config_view config = {},
                  forge::asio::compute::executor compute = {});
   plugin_context(forge::asio::task::scheduler& scheduler, forge::api::core::registry& apis, signal_bus& signals,
                  event_bus& events, diagnostics_store* diagnostics = nullptr, config_view config = {},
                  forge::asio::compute::executor compute = {});
   plugin_context(forge::asio::task::scheduler& scheduler, signal_bus& signals, event_bus& events,
                  diagnostics_store* diagnostics = nullptr, config_view config = {},
                  forge::asio::compute::executor compute = {});

   [[nodiscard]] forge::asio::task::scheduler& scheduler() noexcept;
   [[nodiscard]] bool has_compute() const noexcept;
   [[nodiscard]] forge::asio::compute::executor compute() const;
   [[nodiscard]] forge::api::core::view apis() const noexcept;
   [[nodiscard]] service_view services() const noexcept;
   [[nodiscard]] signal_bus& signals() noexcept;
   [[nodiscard]] event_bus& events() noexcept;
   [[nodiscard]] diagnostics_store* diagnostics() noexcept;
   [[nodiscard]] const config_view& config() const noexcept;
   [[nodiscard]] std::optional<std::string> config_value(const std::string& key) const;

 private:
   forge::asio::task::scheduler* scheduler_ = nullptr;
   forge::asio::compute::executor compute_;
   forge::api::core::registry* apis_ = nullptr;
   service_view services_;
   signal_bus* signals_ = nullptr;
   event_bus* events_ = nullptr;
   diagnostics_store* diagnostics_ = nullptr;
   config_view config_;
};

} // namespace forge::app
