module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

module forge.plugins.db.objectdb.plugin;

import forge.api.core.binding;
import forge.app.plugin_context;
import forge.config.core.component;
import forge.config.core.decode;
import forge.db.driver;
import forge.db.record;
import forge.exceptions;
import forge.objectdb.store;
import forge.plugins.db.objectdb.exceptions;
import forge.plugins.db.objectdb.types;

#if FORGE_PLUGINS_DB_OBJECTDB_HAS_ROCKSDB
import forge.db.rocksdb;
#endif

#include "details/plugin_impl.hxx"
#include "details/config.hxx"

namespace forge::plugins::db::objectdb {

void plugin::impl::configure(config value) {
   detail::validate_config(value);

   auto configured = std::unordered_map<std::string, std::shared_ptr<managed_store>>{};
   for (const auto& item : value.stores) {
      auto record = std::make_shared<managed_store>();
      record->name = item.name;
      record->driver_name = item.driver;
      record->path = item.path;
      record->family = item.family;
      record->options = detail::parse_options(item);
      configured.emplace(record->name, std::move(record));
   }

   auto lock = std::scoped_lock{mutex};
   const auto state = current.load();
   if (state == phase::started || state == phase::stopping || state == phase::stopped) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "objectdb plugin cannot be configured after startup or stop");
   }

   settings = std::move(value);
   enabled = true;
   stores = std::move(configured);
   current.store(phase::configured);
}

void plugin::impl::initialize() {
   auto lock = std::scoped_lock{mutex};
   if (current.load() == phase::registered) {
      current.store(phase::initialized);
      return;
   }
   if (current.load() == phase::configured) {
      current.store(phase::initialized);
   }
}

void plugin::impl::reject_started_setup() const {
   const auto state = current.load();
   if (state == phase::started || state == phase::stopping || state == phase::stopped) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "objectdb stores can only be added before startup");
   }
}

void plugin::impl::reject_duplicate_name(const std::string& name) const {
   if (stores.contains(name)) {
      FORGE_THROW_EXCEPTION(exceptions::duplicate_store, "objectdb store name is already registered",
                            forge::exceptions::ctx("store", name));
   }
}

void plugin::impl::add_store(std::string name,
                             std::shared_ptr<forge::db::driver> driver,
                             forge::objectdb::store::options options) {
   if (name.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "objectdb store name must not be empty");
   }
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "objectdb driver must not be null",
                            forge::exceptions::ctx("store", name));
   }

   auto record = std::make_shared<managed_store>();
   record->name = std::move(name);
   record->driver_name = "custom";
   record->family = "objectdb";
   record->options = options;
   record->driver = std::move(driver);

   auto lock = std::scoped_lock{mutex};
   reject_started_setup();
   reject_duplicate_name(record->name);
   stores.emplace(record->name, std::move(record));
}

void plugin::impl::start() {
   struct pending_open {
      std::string name;
      store_config config;
      std::shared_ptr<forge::db::driver> driver;
   };

   auto pending = std::vector<pending_open>{};
   {
      auto lock = std::scoped_lock{mutex};
      if (!enabled) {
         current.store(phase::started);
         return;
      }

      const auto state = current.load();
      if (state == phase::stopping || state == phase::stopped) {
         FORGE_THROW_EXCEPTION(exceptions::stopped, "objectdb plugin is stopping");
      }

      for (const auto& item : settings.stores) {
         const auto found = stores.find(item.name);
         if (found == stores.end()) {
            FORGE_THROW_EXCEPTION(exceptions::startup_failed, "objectdb configured store is not registered",
                                  forge::exceptions::ctx("store", item.name));
         }
         if (!found->second->driver) {
            pending.push_back(pending_open{.name = item.name, .config = item});
         }
      }
   }

   for (auto& item : pending) {
      item.driver = detail::make_configured_driver(item.config);
   }

   {
      auto lock = std::scoped_lock{mutex};
      const auto state = current.load();
      if (state == phase::stopping || state == phase::stopped) {
         FORGE_THROW_EXCEPTION(exceptions::stopped, "objectdb plugin is stopping");
      }

      for (auto& item : pending) {
         const auto found = stores.find(item.name);
         if (found == stores.end()) {
            FORGE_THROW_EXCEPTION(exceptions::startup_failed, "objectdb configured store is not registered",
                                  forge::exceptions::ctx("store", item.name));
         }
         if (!found->second->driver) {
            found->second->driver = std::move(item.driver);
         }
      }

      for (auto& [name, record] : stores) {
         if (!record->driver) {
            FORGE_THROW_EXCEPTION(exceptions::startup_failed, "objectdb store has no driver",
                                  forge::exceptions::ctx("store", name));
         }
         record->store = std::make_shared<forge::objectdb::store>(
            record->driver,
            forge::objectdb::store::config{.family = forge::db::family{record->family}},
            record->options);
         record->started = true;
      }
      current.store(phase::started);
   }
}

void plugin::impl::request_stop() noexcept {
   auto lock = std::scoped_lock{mutex};
   if (current.load() == phase::stopped) {
      return;
   }
   current.store(phase::stopping);
}

void plugin::impl::close() {
   auto lock = std::scoped_lock{mutex};
   for (auto& [_, record] : stores) {
      record->store.reset();
      record->driver.reset();
      record->started = false;
   }
   current.store(phase::stopped);
}

std::shared_ptr<managed_store> plugin::impl::find_store(const std::string& name) const {
   auto lock = std::scoped_lock{mutex};
   const auto found = stores.find(name);
   if (found == stores.end()) {
      return {};
   }
   return found->second;
}

std::shared_ptr<managed_store> plugin::impl::require_store(const std::string& name) const {
   auto record = find_store(name);
   if (!record) {
      FORGE_THROW_EXCEPTION(exceptions::unknown_store, "objectdb store is not registered",
                            forge::exceptions::ctx("store", name));
   }
   return record;
}

plugin::impl::opened_store plugin::impl::require_open_store(const std::string& name) const {
   auto lock = std::scoped_lock{mutex};
   const auto found = stores.find(name);
   if (found == stores.end()) {
      FORGE_THROW_EXCEPTION(exceptions::unknown_store, "objectdb store is not registered",
                            forge::exceptions::ctx("store", name));
   }

   const auto& record = found->second;
   const auto state = current.load();
   if ((state != phase::started && state != phase::stopping) || record->store == nullptr || record->driver == nullptr ||
       !record->started) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "objectdb store is not started",
                            forge::exceptions::ctx("store", name));
   }

   return opened_store{.store = record->store, .driver = record->driver};
}

status plugin::impl::current_status() const {
   auto out = status{};
   auto lock = std::scoped_lock{mutex};
   out.stores.reserve(stores.size());
   for (const auto& [_, record] : stores) {
      out.stores.push_back(store_status{
         .name = record->name,
         .driver = record->driver_name,
         .path = record->path,
         .family = record->family,
         .started = record->started,
      });
   }
   return out;
}

} // namespace forge::plugins::db::objectdb

namespace forge::plugins::db::objectdb::detail {

std::shared_ptr<plugin::impl> lifecycle::make_impl() {
   return std::make_shared<plugin::impl>();
}

std::optional<forge::config::core::component_descriptor> lifecycle::describe_config(const std::shared_ptr<plugin::impl>&) {
   return forge::config::core::describe_component<config>("plugins.db.objectdb");
}

boost::asio::awaitable<void> lifecycle::configure(const std::shared_ptr<plugin::impl>& impl,
                                                  forge::config::core::component_view view) {
   impl->configure(decode_config(view));
   co_return;
}

boost::asio::awaitable<void> lifecycle::provide(const std::shared_ptr<plugin::impl>& impl,
                                                forge::api::core::provider& provider) {
   provider.install<api>(std::make_shared<plugin::api_impl>(impl));
   co_return;
}

boost::asio::awaitable<void> lifecycle::initialize(const std::shared_ptr<plugin::impl>& impl,
                                                   forge::app::plugin_context&) {
   impl->initialize();
   co_return;
}

boost::asio::awaitable<void> lifecycle::startup(const std::shared_ptr<plugin::impl>& impl) {
   try {
      impl->start();
   } catch (const exceptions::invalid_config&) {
      throw;
   } catch (const exceptions::duplicate_store&) {
      throw;
   } catch (const exceptions::startup_failed&) {
      throw;
   } catch (const std::exception& error) {
      FORGE_THROW_EXCEPTION(exceptions::startup_failed, "objectdb plugin startup failed",
                            forge::exceptions::ctx("error", error.what()));
   }
   co_return;
}

void lifecycle::request_stop(const std::shared_ptr<plugin::impl>& impl) noexcept {
   impl->request_stop();
}

boost::asio::awaitable<void> lifecycle::shutdown(const std::shared_ptr<plugin::impl>& impl) {
   impl->close();
   co_return;
}

std::shared_ptr<forge::db::driver> make_configured_driver(const store_config& value) {
   if (value.driver == "rocksdb") {
#if FORGE_PLUGINS_DB_OBJECTDB_HAS_ROCKSDB
      return std::make_shared<forge::db::rocksdb::driver>(
         forge::db::rocksdb::config{
            .path = value.path,
            .families = {value.family},
            .create_if_missing = value.create_if_missing,
            .create_missing_column_families = value.create_missing_column_families,
         });
#else
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "objectdb rocksdb driver is not available in this build",
                            forge::exceptions::ctx("store", value.name));
#endif
   }

   FORGE_THROW_EXCEPTION(exceptions::invalid_config, "objectdb store driver is unsupported",
                         forge::exceptions::ctx("store", value.name),
                         forge::exceptions::ctx("driver", value.driver));
}

} // namespace forge::plugins::db::objectdb::detail
