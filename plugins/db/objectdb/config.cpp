module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

module forge.plugins.db.objectdb.plugin;

import forge.config.core.component;
import forge.config.core.decode;
import forge.exceptions;
import forge.objectdb.store;
import forge.plugins.db.objectdb.exceptions;
import forge.plugins.db.objectdb.types;

#include "details/config.hxx"

namespace forge::plugins::db::objectdb::detail {

config decode_config(const forge::config::core::component_view& view) {
   auto decoded = forge::config::core::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                            forge::config::core::format_decode_diagnostics("invalid ObjectDB plugin config",
                                                                      decoded.diagnostics));
   }
   validate_config(decoded.value);
   return std::move(decoded.value);
}

forge::objectdb::store::options parse_options(const store_config& value) {
   auto options = forge::objectdb::store::options{};
   if (value.write_policy == "single-writer") {
      options.writes = forge::objectdb::write_policy::single_writer;
   } else if (value.write_policy == "backend") {
      options.writes = forge::objectdb::write_policy::backend;
   } else {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "objectdb store write-policy is unsupported",
                            forge::exceptions::ctx("store", value.name),
                            forge::exceptions::ctx("write-policy", value.write_policy));
   }
   return options;
}

void validate_config(const config& value) {
   auto names = std::unordered_set<std::string>{};
   for (const auto& item : value.stores) {
      if (item.name.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "objectdb store name must not be empty");
      }
      if (!names.insert(item.name).second) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "objectdb store name is duplicated",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.driver.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "objectdb store driver must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.path.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "objectdb store path must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      if (item.family.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "objectdb store family must not be empty",
                               forge::exceptions::ctx("store", item.name));
      }
      (void)parse_options(item);
   }
}

} // namespace forge::plugins::db::objectdb::detail
