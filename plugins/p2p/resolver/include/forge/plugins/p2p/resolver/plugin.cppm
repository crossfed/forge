module;

#include <forge/api/core/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string>

export module forge.plugins.p2p.resolver.plugin;

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
import forge.net.p2p.protocol;
import forge.plugins.p2p.resolver.types;

export namespace forge::plugins::p2p::resolver {

class plugin final : public forge::app::plugin {
 public:
   plugin();
   ~plugin() override;

   plugin(const plugin&) = delete;
   plugin& operator=(const plugin&) = delete;

   [[nodiscard]] forge::app::plugin_id id() const override;
   [[nodiscard]] std::string version() const override;
   [[nodiscard]] std::optional<forge::config::core::component_descriptor> describe_config() const override;
   boost::asio::awaitable<void> configure(forge::config::core::component_view view) override;
   boost::asio::awaitable<void> provide(forge::api::core::provider& provider) override;
   boost::asio::awaitable<void> initialize(forge::app::plugin_context& context) override;
   boost::asio::awaitable<void> startup() override;
   void request_stop() noexcept override;
   boost::asio::awaitable<void> shutdown() override;

 private:
   struct impl;
   class resolver_protocol_service;
   class api_impl;
   std::shared_ptr<impl> impl_;
};

[[nodiscard]] forge::app::plugin_descriptor descriptor();
[[nodiscard]] forge::net::p2p::protocol_id default_protocol();

} // namespace forge::plugins::p2p::resolver

#include "details/resolver_protocol.hxx"
#include "details/resolver_protocol_service.hxx"

FORGE_API(::forge::plugins::p2p::resolver::detail::resolver_protocol,
          FORGE_API_CONTRACT("forge.plugins.p2p.resolver.protocol", 1, 0),
          FORGE_API_METHOD(query))
