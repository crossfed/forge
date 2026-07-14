#pragma once

#include "native_transaction_owner.hxx"

namespace forge::plugins::db::rocksdb {

class plugin::transaction_owner_impl final : public native_transaction_owner {
 public:
   explicit transaction_owner_impl(std::shared_ptr<impl> owner);

   [[nodiscard]] std::pair<std::shared_ptr<forge::rocksdb::store>, forge::asio::task::scheduler*>
   require_running() const override;
   void track_transaction(std::shared_ptr<native_transaction_control> transaction) override;

 private:
   std::shared_ptr<impl> owner_;
};

} // namespace forge::plugins::db::rocksdb
