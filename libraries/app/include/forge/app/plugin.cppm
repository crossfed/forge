module;

#include <boost/asio/awaitable.hpp>

#include <optional>
#include <string>
#include <compare>

export module forge.app.plugin;

import forge.config.component;
import forge.api.core.exceptions;
import forge.api.core.types;
import forge.api.core.descriptor;
import forge.api.core.error_projection;
import forge.api.core.handle;
import forge.api.core.connection;
import forge.api.core.registry;
import forge.api.core.binding;
import forge.api.core.dispatcher;
import forge.app.plugin_context;

export namespace forge::app {

enum class plugin_state {
   registered,
   initialized,
   started,
   stopped,
};

struct plugin_id {
   std::string value;

   [[nodiscard]] friend bool operator==(const plugin_id&, const plugin_id&) = default;
   [[nodiscard]] friend auto operator<=>(const plugin_id&, const plugin_id&) = default;
};

struct plugin_config {
   plugin_id id;
   bool enabled = true;
};

class plugin {
 public:
   virtual ~plugin() = default;

   [[nodiscard]] virtual plugin_id id() const = 0;
   [[nodiscard]] virtual std::string version() const = 0;

   [[nodiscard]] virtual std::optional<config::component_descriptor> describe_config() const;
   virtual boost::asio::awaitable<void> configure(config::component_view view);
   virtual boost::asio::awaitable<void> provide(forge::api::core::provider& provider);
   virtual boost::asio::awaitable<void> initialize(plugin_context& context) = 0;
   virtual boost::asio::awaitable<void> startup() = 0;
   virtual void request_stop() noexcept;
   virtual boost::asio::awaitable<void> shutdown() = 0;
};

[[nodiscard]] bool valid_plugin_id(const plugin_id& id) noexcept;

} // namespace forge::app
