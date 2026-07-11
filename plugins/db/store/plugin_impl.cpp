module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
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

module forge.plugins.db.store.plugin;

import forge.api.core.binding;
import forge.app.plugin_context;
import forge.config.core.component;
import forge.config.core.decode;
import forge.db.blob.store;
import forge.db.core.driver;
import forge.db.core.record;
import forge.exceptions;
import forge.db.object.store;
import forge.plugins.db.store.exceptions;
import forge.plugins.db.store.types;

#if FORGE_PLUGINS_DB_STORE_HAS_ROCKSDB
import forge.db.rocksdb.driver;
import forge.rocksdb.types;
#endif

#include "details/plugin_impl.hxx"
#include "details/config.hxx"

namespace forge::plugins::db::store {

void plugin::impl::configure(config value) {
   detail::validate_config(value);

   auto configured = std::unordered_map<std::string, std::shared_ptr<managed_store>>{};
   for (const auto& item : value.stores) {
      auto record = std::make_shared<managed_store>();
      record->name = item.name;
      record->driver_name = item.driver;
      record->path = item.path;
      record->options = detail::parse_options(item);
      configured.emplace(record->name, std::move(record));
   }

   auto lock = std::scoped_lock{mutex};
   const auto state = current.load();
   if (state == phase::started || state == phase::stopping || state == phase::stopped) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "db store plugin cannot be configured after startup or stop");
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
      FORGE_THROW_EXCEPTION(exceptions::stopped, "db stores can only be added before startup");
   }
}

void plugin::impl::reject_duplicate_name(const std::string& name) const {
   if (stores.contains(name)) {
      FORGE_THROW_EXCEPTION(exceptions::duplicate_store, "db store name is already registered",
                            forge::exceptions::ctx("store", name));
   }
}

void plugin::impl::add_store(std::string name,
                             std::shared_ptr<forge::db::core::driver> driver,
                             store_options options) {
   if (name.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "db store name must not be empty");
   }
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "db store driver must not be null",
                            forge::exceptions::ctx("store", name));
   }
   if (!options.object && !options.blob) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, "db store must configure object or blob layer",
                            forge::exceptions::ctx("store", name));
   }
   detail::validate_options(options, name, true);

   auto record = std::make_shared<managed_store>();
   record->name = std::move(name);
   record->driver_name = "custom";
   record->options = std::move(options);
   record->driver = std::move(driver);

   auto lock = std::scoped_lock{mutex};
   reject_started_setup();
   reject_duplicate_name(record->name);
   stores.emplace(record->name, std::move(record));
}

void plugin::impl::start() {
   auto pending = std::vector<pending_open>{};
   {
      auto lock = std::scoped_lock{mutex};
      if (!enabled) {
         current.store(phase::started);
         return;
      }

      const auto state = current.load();
      if (state == phase::stopping || state == phase::stopped) {
         FORGE_THROW_EXCEPTION(exceptions::stopped, "db store plugin is stopping");
      }

      for (const auto& item : settings.stores) {
         const auto found = stores.find(item.name);
         if (found == stores.end()) {
            FORGE_THROW_EXCEPTION(exceptions::startup_failed, "db store configured store is not registered",
                                  forge::exceptions::ctx("store", item.name));
         }
         if (!found->second->driver) {
            pending.push_back(pending_open{.name = item.name, .config = item});
         }
      }
   }

   for (auto& item : pending) {
      item.driver = make_configured_driver(item.config);
   }

   {
      auto lock = std::scoped_lock{mutex};
      const auto state = current.load();
      if (state == phase::stopping || state == phase::stopped) {
         FORGE_THROW_EXCEPTION(exceptions::stopped, "db store plugin is stopping");
      }

      for (auto& item : pending) {
         const auto found = stores.find(item.name);
         if (found == stores.end()) {
            FORGE_THROW_EXCEPTION(exceptions::startup_failed, "db store configured store is not registered",
                                  forge::exceptions::ctx("store", item.name));
         }
         if (!found->second->driver) {
            found->second->driver = std::move(item.driver);
         }
      }

      for (auto& [name, record] : stores) {
         if (!record->driver) {
            FORGE_THROW_EXCEPTION(exceptions::startup_failed, "db store has no driver",
                                  forge::exceptions::ctx("store", name));
         }
         if (record->options.object) {
            record->objects = std::make_shared<forge::db::object::store>(
               record->driver,
               forge::db::object::store::config{.family = record->options.object->family},
               record->options.object->runtime);
         }
         if (record->options.blob) {
            record->blobs = std::make_shared<forge::db::blob::store>(
               record->driver,
               forge::db::blob::store::config{
                  .data_family = record->options.blob->data_family,
                  .refs_family = record->options.blob->refs_family,
               });
         }
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
      record->objects.reset();
      record->blobs.reset();
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
      FORGE_THROW_EXCEPTION(exceptions::unknown_store, "db store is not registered",
                            forge::exceptions::ctx("store", name));
   }
   return record;
}

plugin::impl::opened_store plugin::impl::require_open_store(const std::string& name) const {
   auto lock = std::scoped_lock{mutex};
   const auto found = stores.find(name);
   if (found == stores.end()) {
      FORGE_THROW_EXCEPTION(exceptions::unknown_store, "db store is not registered",
                            forge::exceptions::ctx("store", name));
   }

   const auto& record = found->second;
   const auto state = current.load();
   if ((state != phase::started && state != phase::stopping) || record->driver == nullptr ||
       !record->started) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "db store is not started",
                            forge::exceptions::ctx("store", name));
   }

   return opened_store{.driver = record->driver, .objects = record->objects, .blobs = record->blobs};
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
         .object = record->options.object.has_value(),
         .blob = record->options.blob.has_value(),
         .started = record->started,
      });
   }
   return out;
}

} // namespace forge::plugins::db::store

namespace forge::plugins::db::store {
namespace {

#if FORGE_PLUGINS_DB_STORE_HAS_ROCKSDB
[[nodiscard]] forge::rocksdb::compression_type parse_blob_compression(const std::string& value,
                                                                      const std::string& store_name) {
   if (value == "none") {
      return forge::rocksdb::compression_type::none;
   }
   if (value == "snappy") {
      return forge::rocksdb::compression_type::snappy;
   }
   if (value == "zlib") {
      return forge::rocksdb::compression_type::zlib;
   }
   if (value == "bzip2") {
      return forge::rocksdb::compression_type::bzip2;
   }
   if (value == "lz4") {
      return forge::rocksdb::compression_type::lz4;
   }
   if (value == "lz4hc") {
      return forge::rocksdb::compression_type::lz4hc;
   }
   if (value == "xpress") {
      return forge::rocksdb::compression_type::xpress;
   }
   if (value == "zstd") {
      return forge::rocksdb::compression_type::zstd;
   }

   FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store blob compression type is unsupported",
                         forge::exceptions::ctx("store", store_name),
                         forge::exceptions::ctx("blob-compression-type", value));
}

[[nodiscard]] forge::rocksdb::blob_options to_rocksdb_blob_options(const blob_data_options& value,
                                                                   const std::string& store_name) {
   return forge::rocksdb::blob_options{
      .enable_blob_files = value.enable_blob_files,
      .min_blob_size = value.min_blob_size,
      .blob_file_size = value.blob_file_size,
      .blob_compression_type = parse_blob_compression(value.blob_compression_type, store_name),
      .enable_blob_garbage_collection = value.enable_blob_garbage_collection,
      .blob_garbage_collection_age_cutoff = value.blob_garbage_collection_age_cutoff,
   };
}

void add_family_once(std::vector<forge::rocksdb::column_family_config>& families,
                     forge::rocksdb::column_family_config value) {
   const auto found = std::ranges::find_if(families, [&](const auto& item) {
      return item.name == value.name;
   });
   if (found == families.end()) {
      families.push_back(std::move(value));
   }
}

[[nodiscard]] std::vector<forge::rocksdb::column_family_config> configured_families(const store_config& value) {
   auto families = std::vector<forge::rocksdb::column_family_config>{};
   if (value.object) {
      add_family_once(families, forge::rocksdb::column_family_config{value.object->family});
   }
   if (value.blob) {
      auto data_family = forge::rocksdb::column_family_config{value.blob->data_family};
      data_family.blobs = to_rocksdb_blob_options(value.blob->data_blobs, value.name);
      add_family_once(families, std::move(data_family));
      add_family_once(families, forge::rocksdb::column_family_config{value.blob->refs_family});
   }
   return families;
}
#endif

} // namespace

std::shared_ptr<forge::db::core::driver> plugin::impl::make_configured_driver(const store_config& value) {
   if (value.driver == "rocksdb") {
#if FORGE_PLUGINS_DB_STORE_HAS_ROCKSDB
      return std::make_shared<forge::db::rocksdb::driver>(
         forge::db::rocksdb::config{
            .path = value.path,
            .families = configured_families(value),
            .create_if_missing = value.create_if_missing,
            .create_missing_column_families = value.create_missing_column_families,
         });
#else
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store rocksdb driver is not available in this build",
                            forge::exceptions::ctx("store", value.name));
#endif
   }

   FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store driver is unsupported",
                         forge::exceptions::ctx("store", value.name),
                         forge::exceptions::ctx("driver", value.driver));
}

} // namespace forge::plugins::db::store
