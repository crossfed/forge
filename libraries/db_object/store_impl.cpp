module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <map>
#include <memory>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

module forge.db.object.store;

import forge.db.exceptions;
import forge.db.object.exceptions;

#include "details/store_impl.hxx"

namespace forge::db::object {

store::impl::impl(std::shared_ptr<forge::db::driver> driver_value,
                  store::config config_value,
                  store::options options_value)
    : driver{std::move(driver_value)},
      config{std::move(config_value)},
      settings{options_value},
      write_gate{std::make_shared<detail::write_gate>()} {
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object driver is null");
   }
   if (config.family.name.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object family is empty");
   }
}

boost::asio::awaitable<forge::db::transaction> store::impl::open_write_transaction() const {
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object driver is null");
   }
   co_return co_await driver->begin_transaction();
}

boost::asio::awaitable<forge::db::snapshot> store::impl::open_read_snapshot() const {
   if (!driver) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object driver is null");
   }
   co_return co_await driver->begin_read();
}

void store::impl::register_object_type(forge::ids::object_id type, std::type_index model) {
   if (registered.contains(type)) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db object type is already registered");
   }
   registered.emplace(type, model);
}

void store::impl::ensure_registered_type(forge::ids::object_id type, std::type_index model) const {
   const auto found = registered.find(type);
   if (found == registered.end() || found->second != model) {
      FORGE_THROW_EXCEPTION(exceptions::unregistered_object, "db object type is not registered");
   }
}

} // namespace forge::db::object
