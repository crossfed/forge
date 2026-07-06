module;

#include <boost/asio/awaitable.hpp>
#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module forge.db.rocksdb;

import forge.db.exceptions;
import forge.db.record;
import forge.rocksdb.store;

namespace forge::db::rocksdb {
namespace {

forge::rocksdb::family native_family(const forge::db::family& value) {
   return forge::rocksdb::family{value.name};
}

forge::db::record_page to_record_page(forge::rocksdb::scan_result scan,
                                      const forge::db::record_range& range,
                                      const forge::db::page_request& request) {
   forge::db::validate_page_request(request);

   auto result = forge::db::record_page{};
   auto last_returned = std::optional<forge::db::record_key>{};
   auto stopped_at_range_end = false;
   auto has_more_in_range = false;
   for (auto& entry : scan.entries) {
      auto key = forge::db::record_key{std::move(entry.key)};
      if (range.has_end && !(key.bytes() < range.end.bytes())) {
         stopped_at_range_end = true;
         break;
      }
      if (result.entries.size() >= request.limit) {
         has_more_in_range = true;
         break;
      }
      last_returned = key;
      result.entries.push_back(forge::db::record_entry{.key = std::move(key), .value = std::move(entry.value)});
   }

   if (!stopped_at_range_end && last_returned.has_value()
       && (has_more_in_range || (!range.has_end && !scan.next_cursor.empty()))) {
      result.next = forge::db::cursor{.boundary = std::move(*last_returned)};
   }
   return result;
}

forge::rocksdb::scan_request make_scan_request(const forge::db::record_range& range,
                                               const forge::db::page_request& request) {
   auto limit = request.limit;
   if (range.has_end && limit > 0) {
      ++limit;
   }

   return forge::rocksdb::scan_request{
      .prefix = range.prefix.bytes(),
      .cursor = request.after ? request.after->boundary.bytes() : std::vector<std::byte>{},
      .limit = limit,
      .options = {},
      .lower_bound = range.begin.bytes(),
   };
}

class transaction_session final : public forge::db::session {
 public:
   explicit transaction_session(forge::rocksdb::transaction transaction) : transaction_{std::move(transaction)} {}

   [[nodiscard]] forge::db::capabilities capabilities() const noexcept override {
      return forge::db::capabilities{.snapshot_reads = false, .writes = true};
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::db::family column_family,
                                                                     forge::db::record_key key) override {
      co_return transaction_.get(native_family(column_family), key.bytes());
   }

   boost::asio::awaitable<void> put(forge::db::family column_family,
                                    forge::db::record_key key,
                                    std::vector<std::byte> value) override {
      transaction_.put(native_family(column_family), key.bytes(), std::move(value));
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::db::family column_family, forge::db::record_key key) override {
      transaction_.erase(native_family(column_family), key.bytes());
      co_return;
   }

   boost::asio::awaitable<forge::db::record_page> scan_page(forge::db::family column_family,
                                                            forge::db::record_range range,
                                                            forge::db::page_request request) override {
      auto scan = transaction_.scan_page(native_family(column_family), make_scan_request(range, request));
      co_return to_record_page(std::move(scan), range, request);
   }

   boost::asio::awaitable<void> commit() override {
      transaction_.commit();
      co_return;
   }

   boost::asio::awaitable<void> rollback() override {
      transaction_.rollback();
      co_return;
   }

 private:
   forge::rocksdb::transaction transaction_;
};

class snapshot_session final : public forge::db::session {
 public:
   explicit snapshot_session(forge::rocksdb::snapshot snapshot) : snapshot_{std::move(snapshot)} {}

   [[nodiscard]] forge::db::capabilities capabilities() const noexcept override {
      return forge::db::capabilities{.snapshot_reads = true, .writes = false};
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::db::family column_family,
                                                                     forge::db::record_key key) override {
      co_return snapshot_.get(native_family(column_family), key.bytes());
   }

   boost::asio::awaitable<void> put(forge::db::family, forge::db::record_key, std::vector<std::byte>) override {
      FORGE_THROW_EXCEPTION(forge::db::exceptions::unsupported_operation, "db RocksDB snapshot is read-only");
   }

   boost::asio::awaitable<void> erase(forge::db::family, forge::db::record_key) override {
      FORGE_THROW_EXCEPTION(forge::db::exceptions::unsupported_operation, "db RocksDB snapshot is read-only");
   }

   boost::asio::awaitable<forge::db::record_page> scan_page(forge::db::family column_family,
                                                            forge::db::record_range range,
                                                            forge::db::page_request request) override {
      auto scan = snapshot_.scan_page(native_family(column_family), make_scan_request(range, request));
      co_return to_record_page(std::move(scan), range, request);
   }

   boost::asio::awaitable<void> commit() override {
      FORGE_THROW_EXCEPTION(forge::db::exceptions::unsupported_operation, "db RocksDB snapshot cannot commit");
   }

   boost::asio::awaitable<void> rollback() override {
      co_return;
   }

 private:
   forge::rocksdb::snapshot snapshot_;
};

} // namespace

driver::driver(config value)
    : store_{std::make_shared<forge::rocksdb::store>(forge::rocksdb::config{
         .path = std::move(value.path),
         .column_families = std::move(value.families),
         .create_if_missing = value.create_if_missing,
         .create_missing_column_families = value.create_missing_column_families,
      })},
      write_{value.write} {}

boost::asio::awaitable<std::unique_ptr<forge::db::session>> driver::open_transaction() {
   co_return std::make_unique<transaction_session>(store_->begin(write_));
}

boost::asio::awaitable<std::unique_ptr<forge::db::session>> driver::open_snapshot() {
   co_return std::make_unique<snapshot_session>(store_->begin_snapshot());
}

boost::asio::awaitable<void> driver::async_flush(bool sync) {
   flush(sync);
   co_return;
}

void driver::flush(bool sync) {
   store_->flush_wal(sync);
}

} // namespace forge::db::rocksdb
