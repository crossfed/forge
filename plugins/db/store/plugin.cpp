module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <exception>

module forge.plugins.db.store.plugin;

import forge.api.core.binding;
import forge.app.plugin;
import forge.app.plugin_context;
import forge.config.core.component;
import forge.config.core.decode;
import forge.db.blob.store;
import forge.db.core.driver;
import forge.db.object.store;
import forge.exceptions;
import forge.plugins.db.store.exceptions;

#include "details/api_impl.hxx"
#include "details/config.hxx"
#include "details/plugin_impl.hxx"

namespace forge::plugins::db::store {

plugin::plugin() : impl_{std::make_shared<impl>()} {}
plugin::~plugin() = default;

forge::app::plugin_id plugin::id() const {
   return forge::app::plugin_id{.value = "forge.plugins.db.store"};
}

std::string plugin::version() const {
   return "1.0.0";
}

std::optional<forge::config::core::component_descriptor> plugin::describe_config() const {
   return forge::config::core::describe_component<config>("plugins.db.store");
}

boost::asio::awaitable<void> plugin::configure(forge::config::core::component_view view) {
   impl_->configure(detail::decode_config(view));
   co_return;
}

boost::asio::awaitable<void> plugin::provide(forge::api::core::provider& provider) {
   provider.install<api>(std::make_shared<api_impl>(impl_));
   co_return;
}

boost::asio::awaitable<void> plugin::initialize(forge::app::plugin_context& context) {
   static_cast<void>(context);
   impl_->initialize();
   co_return;
}

boost::asio::awaitable<void> plugin::startup() {
   try {
      co_await impl_->start();
   } catch (const exceptions::invalid_config&) {
      throw;
   } catch (const exceptions::duplicate_store&) {
      throw;
   } catch (const exceptions::startup_failed&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::startup_failed, "db store plugin startup failed",
                            forge::exceptions::ctx("error", error.what()));
   }
   co_return;
}

void plugin::request_stop() noexcept {
   impl_->request_stop();
}

boost::asio::awaitable<void> plugin::shutdown() {
   impl_->close();
   co_return;
}

forge::app::plugin_descriptor descriptor() {
   return forge::app::plugin_descriptor{
      .id = forge::app::plugin_id{.value = "forge.plugins.db.store"},
      .factory = [] {
         return std::make_unique<plugin>();
      },
   };
}

} // namespace forge::plugins::db::store
