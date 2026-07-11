module;

#include <forge/exceptions/macros.hpp>

#include <memory>
#include <mutex>
#include <utility>

module forge.plugins.db.rocksdb.plugin;

import forge.asio.task_scheduler;
import forge.plugins.db.rocksdb.exceptions;
import forge.rocksdb.store;

#include "details/native_transaction_control.hxx"
#include "details/plugin_impl.hxx"
#include "details/transaction_owner_impl.hxx"

namespace forge::plugins::db::rocksdb {

plugin::transaction_owner_impl::transaction_owner_impl(std::shared_ptr<impl> owner)
   : owner_{std::move(owner)} {}

std::pair<std::shared_ptr<forge::rocksdb::store>, forge::asio::task_scheduler*>
plugin::transaction_owner_impl::require_running() const {
   if (owner_ == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "rocksdb plugin is not started");
   }
   return owner_->require_running();
}

void plugin::transaction_owner_impl::track_transaction(
   std::shared_ptr<native_transaction_control> transaction) {
   if (owner_ == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::stopped, "rocksdb plugin is not started");
   }
   owner_->track_transaction(std::move(transaction));
}

} // namespace forge::plugins::db::rocksdb
