#pragma once

#include "native_transaction_owner.hxx"

namespace forge::plugins::db::rocksdb {

struct native_transaction_state {
   native_transaction_state(std::shared_ptr<native_transaction_owner> owner,
                            forge::rocksdb::transaction transaction);

   std::shared_ptr<native_transaction_owner> owner;
   std::optional<forge::rocksdb::transaction> transaction;
   std::mutex mutex;
   bool closed_by_owner = false;
};

} // namespace forge::plugins::db::rocksdb
