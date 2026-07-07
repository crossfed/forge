module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

module forge.plugins.db.object.plugin;

import forge.config.component;
import forge.config.decode;
import forge.exceptions;
import forge.db.object.store;
import forge.plugins.db.object.exceptions;
import forge.plugins.db.object.types;

#include "details/config.hxx"

namespace forge::plugins::db::object::detail {

config decode_config(const forge::config::component_view& view) {
   auto decoded = forge::config::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                            forge::config::format_decode_diagnostics("invalid DB Object plugin config",
                                                                      decoded.diagnostics));
   }
   validate_config(decoded.value);
   return std::move(decoded.value);
}

forge::db::object::store::options parse_options(const store_config& value) {
   auto options = forge::db::object::store::options{};
   if (value.write_policy == "single-writer") {
      options.writes = forge::db::object::write_policy::single_writer;
   } else if (value.write_policy == "backend") {
      options.writes = forge::db::object::write_policy::backend;
   } else {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db object store write-policy is unsupported",
                            forge::exceptions::ctx("store", value.name),
                            forge::exceptions::ctx("write-policy", value.write_policy));
   }
   return options;
}

void validate_config(const config& value) {
   auto names = std::unordered_set<std::string>{};
   for (const auto& item : value.stores) {
      if (item.name.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db object store name must not be empty");
      }
      if (!names.insert(item.name).second) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db object store name is duplicated",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.driver.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db object store driver must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.path.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db object store path must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.family.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "db object store family must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      (void)parse_options(item);
   }
}

} // namespace forge::plugins::db::object::detail
