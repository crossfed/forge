#pragma once

namespace forge::plugins::db::rocksdb {

enum class phase : std::uint8_t {
   registered,
   configured,
   initialized,
   started,
   stopping,
   stopped,
};

struct native_transaction_control;

struct plugin::impl {
   mutable std::mutex mutex;
   config settings;
   std::shared_ptr<forge::rocksdb::store> store;
   forge::asio::task_scheduler* scheduler = nullptr;
   std::vector<std::weak_ptr<native_transaction_control>> transactions;
   std::atomic<phase> current = phase::registered;

   void configure(config value);
   void set_scheduler(forge::asio::task_scheduler& value);
   void open();
   void request_stop() noexcept;
   void close();
   void track_transaction(std::shared_ptr<native_transaction_control> transaction);
   void release_transactions() noexcept;

   [[nodiscard]] std::pair<std::shared_ptr<forge::rocksdb::store>, forge::asio::task_scheduler*> require_running() const;
};

} // namespace forge::plugins::db::rocksdb
