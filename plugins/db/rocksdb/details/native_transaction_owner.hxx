#pragma once

namespace forge::plugins::db::rocksdb {

struct native_transaction_control;

struct native_transaction_owner {
   virtual ~native_transaction_owner() = default;

   [[nodiscard]] virtual std::pair<std::shared_ptr<forge::rocksdb::store>, forge::asio::task::scheduler*>
   require_running() const = 0;
   virtual void track_transaction(std::shared_ptr<native_transaction_control> transaction) = 0;
};

} // namespace forge::plugins::db::rocksdb
