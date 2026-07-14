module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

module forge.plugins.db.rocksdb.plugin;

import forge.asio.task;
import forge.config.core.component;
import forge.config.core.decode;
import forge.exceptions;
import forge.plugins.db.rocksdb.exceptions;
import forge.rocksdb.store;

#include "details/plugin_impl.hxx"
#include "details/config.hxx"

namespace forge::plugins::db::rocksdb::detail {

config decode_config(const forge::config::core::component_view& view) {
   auto decoded = forge::config::core::decode<config>(view.source(), view.section());
   if (!decoded.ok()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                          forge::config::core::format_decode_diagnostics("invalid RocksDB config",
                                                                 decoded.diagnostics));
   }
   return std::move(decoded.value);
}

void validate_config(const config& value) {
   if (value.path.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config, "rocksdb path must not be empty");
   }
   for (const auto& family : value.column_families) {
      if (family.name.empty()) {
         FORGE_THROW_EXCEPTION(exceptions::invalid_config, "rocksdb column family name must not be empty");
      }
   }
}

} // namespace forge::plugins::db::rocksdb::detail
