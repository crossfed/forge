module;

#include <memory>
#include <mutex>
#include <optional>
#include <utility>

module forge.plugins.db.rocksdb.plugin;

import forge.asio.task_scheduler;
import forge.rocksdb.store;

#include "details/native_transaction_state.hxx"

namespace forge::plugins::db::rocksdb {

native_transaction_state::native_transaction_state(
   std::shared_ptr<native_transaction_owner> owner_value,
   forge::rocksdb::transaction transaction_value)
   : owner{std::move(owner_value)}, transaction{std::move(transaction_value)} {}

} // namespace forge::plugins::db::rocksdb
