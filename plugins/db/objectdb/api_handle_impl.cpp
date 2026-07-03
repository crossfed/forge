module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <memory>
#include <string>
#include <utility>

module forge.plugins.db.objectdb.api;

import forge.objectdb.hooks;
import forge.objectdb.snapshot;
import forge.objectdb.store;
import forge.objectdb.transaction;

namespace forge::plugins::db::objectdb {

std::string store_handle::name() const {
   if (!state_) {
      return {};
   }
   return state_->name();
}

std::shared_ptr<forge::objectdb::store> store_handle::require_store() const {
   if (!state_) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "objectdb store handle is empty");
   }
   return state_->require_store();
}

void store_handle::add_interceptor(std::shared_ptr<forge::objectdb::interceptor> value) const {
   require_store()->add_interceptor(std::move(value));
}

void store_handle::add_observer(std::shared_ptr<forge::objectdb::observer> value) const {
   require_store()->add_observer(std::move(value));
}

boost::asio::awaitable<forge::objectdb::transaction> store_handle::begin_transaction() const {
   co_return co_await require_store()->begin_transaction();
}

boost::asio::awaitable<forge::objectdb::snapshot> store_handle::begin_read() const {
   co_return co_await require_store()->begin_read();
}

} // namespace forge::plugins::db::objectdb
