#pragma once

namespace forge::plugins::db::rocksdb {

struct native_transaction_control {
   virtual ~native_transaction_control() = default;
   virtual void release_native() noexcept = 0;
};

} // namespace forge::plugins::db::rocksdb
