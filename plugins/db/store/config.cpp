module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

module forge.plugins.db.store.plugin;

import forge.config.core.component;
import forge.config.core.decode;
import forge.db.core.record;
import forge.db.object.store;
import forge.exceptions;
import forge.plugins.db.store.exceptions;
import forge.plugins.db.store.types;

#include "details/config.hxx"

namespace forge::plugins::db::store::detail {

config decode_config(const forge::config::core::component_view& view) {
   auto decoded = forge::config::core::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                            forge::config::core::format_decode_diagnostics("invalid DB Store plugin config",
                                                                            decoded.diagnostics));
   }
   validate_config(decoded.value);
   return std::move(decoded.value);
}

forge::db::object::store::options parse_object_options(const object_layer_config& value,
                                                       const std::string& store_name) {
   auto options = forge::db::object::store::options{};
   if (value.write_policy == "single-writer") {
      options.writes = forge::db::object::write_policy::single_writer;
   } else if (value.write_policy == "backend") {
      options.writes = forge::db::object::write_policy::backend;
   } else {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store object write-policy is unsupported",
                            forge::exceptions::ctx("store", store_name),
                            forge::exceptions::ctx("write-policy", value.write_policy));
   }
   return options;
}

store_options parse_options(const store_config& value) {
   auto options = store_options{
      .object = std::nullopt,
      .blob = std::nullopt,
      .revision = std::nullopt,
   };
   if (value.object) {
      options.object = object_layer_options{
         .family = forge::db::core::family{value.object->family},
         .runtime = parse_object_options(*value.object, value.name),
      };
   }
   if (value.blob) {
      options.blob = blob_layer_options{
         .data_family = forge::db::core::family{value.blob->data_family},
         .refs_family = forge::db::core::family{value.blob->refs_family},
      };
   }
   if (value.revision) {
      options.revision = revision_layer_options{};
   }
   return options;
}

void validate_options(const store_options& value, const std::string& store_name, bool programmatic) {
   const auto fail = [&](const char* message, const std::string& family) {
      if (programmatic) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_argument,
                               message,
                               forge::exceptions::ctx("store", store_name),
                               forge::exceptions::ctx("family", family));
      }
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                            message,
                            forge::exceptions::ctx("store", store_name),
                            forge::exceptions::ctx("family", family));
   };

   if (value.revision && !value.object) {
      if (programmatic) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_argument,
                               "db store revision layer requires object layer",
                               forge::exceptions::ctx("store", store_name));
      }
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                            "db store revision layer requires object layer",
                            forge::exceptions::ctx("store", store_name));
   }

   if (value.object && value.blob) {
      if (value.object->family.name == value.blob->data_family.name) {
         fail("db store object and blob data families must be distinct", value.object->family.name);
      }
      if (value.object->family.name == value.blob->refs_family.name) {
         fail("db store object and blob refs families must be distinct", value.object->family.name);
      }
   }
   if (value.blob && value.blob->data_family.name == value.blob->refs_family.name) {
      fail("db store blob data and refs families must be distinct", value.blob->data_family.name);
   }
}

void validate_config(const config& value) {
   auto names = std::unordered_set<std::string>{};
   for (const auto& item : value.stores) {
      if (item.name.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store name must not be empty");
      }
      if (!names.insert(item.name).second) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store name is duplicated",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.driver.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store driver must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.path.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store path must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (!item.object && !item.blob) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store must configure object or blob layer",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.revision && !item.object) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                               "db store revision layer requires object layer",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.object && item.object->family.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store object family must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.blob && item.blob->data_family.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store blob data family must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.blob && item.blob->refs_family.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db store blob refs family must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      validate_options(parse_options(item), item.name, false);
   }
}

} // namespace forge::plugins::db::store::detail
