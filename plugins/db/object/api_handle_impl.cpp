module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <memory>
#include <string>
#include <utility>

module forge.plugins.db.object.api;

import forge.db.object.hooks;
import forge.db.object.snapshot;
import forge.db.object.store;
import forge.db.object.transaction;

namespace forge::plugins::db::object {

std::string store_handle::name() const {
   if (!state_) {
      return {};
   }
   return state_->name();
}

std::shared_ptr<forge::db::object::store> store_handle::require_store() const {
   if (!state_) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "db object store handle is empty");
   }
   return state_->require_store();
}

void store_handle::add_interceptor(std::shared_ptr<forge::db::object::interceptor> value) const {
   require_store()->add_interceptor(std::move(value));
}

void store_handle::add_observer(std::shared_ptr<forge::db::object::observer> value) const {
   require_store()->add_observer(std::move(value));
}

boost::asio::awaitable<forge::db::object::transaction> store_handle::begin_transaction() const {
   co_return co_await require_store()->begin_transaction();
}

boost::asio::awaitable<forge::db::object::snapshot> store_handle::begin_read() const {
   co_return co_await require_store()->begin_read();
}

} // namespace forge::plugins::db::object
