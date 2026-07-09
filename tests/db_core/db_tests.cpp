#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.core.driver;
import forge.db.core.record;

namespace {

using record_map = std::map<forge::db::core::record_key, std::vector<std::byte>>;
using family_map = std::map<std::string, record_map>;

std::vector<std::byte> bytes(std::string text) {
   return std::vector<std::byte>{
      reinterpret_cast<const std::byte*>(text.data()),
      reinterpret_cast<const std::byte*>(text.data() + text.size())};
}

std::string text(const std::vector<std::byte>& bytes_value) {
   return std::string{
      reinterpret_cast<const char*>(bytes_value.data()),
      reinterpret_cast<const char*>(bytes_value.data() + bytes_value.size())};
}

forge::db::core::record_key key(std::string text_value) {
   return forge::db::core::record_key{bytes(std::move(text_value))};
}

bool starts_with(const forge::db::core::record_key& value, const forge::db::core::record_key& prefix) {
   const auto& bytes_value = value.bytes();
   const auto& prefix_value = prefix.bytes();
   return prefix_value.empty() ||
          (bytes_value.size() >= prefix_value.size() &&
           std::equal(prefix_value.begin(), prefix_value.end(), bytes_value.begin()));
}

struct memory_state {
   family_map records;
   bool fail_commit = false;
   std::size_t rollback_calls = 0;
};

class memory_session final : public forge::db::core::session {
 public:
   memory_session(std::shared_ptr<memory_state> state, bool snapshot, bool writable)
       : state_{std::move(state)}, working_{state_->records}, snapshot_{snapshot}, writable_{writable} {}

   [[nodiscard]] forge::db::core::capabilities capabilities() const noexcept override {
      return forge::db::core::capabilities{.snapshot_reads = snapshot_, .writes = writable_};
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::db::core::family family,
                                                                     forge::db::core::record_key record) override {
      const auto family_found = working_.find(family.name);
      if (family_found == working_.end()) {
         co_return std::nullopt;
      }
      const auto found = family_found->second.find(record);
      if (found == family_found->second.end()) {
         co_return std::nullopt;
      }
      co_return found->second;
   }

   boost::asio::awaitable<void> put(forge::db::core::family family,
                                    forge::db::core::record_key record,
                                    std::vector<std::byte> value) override {
      working_[family.name][std::move(record)] = std::move(value);
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::db::core::family family, forge::db::core::record_key record) override {
      working_[family.name].erase(record);
      co_return;
   }

   boost::asio::awaitable<forge::db::core::record_page> scan_page(forge::db::core::family family,
                                                            forge::db::core::record_range range,
                                                            forge::db::core::page_request request) override {
      forge::db::core::validate_page_request(request);
      auto result = forge::db::core::record_page{};
      const auto family_found = working_.find(family.name);
      if (family_found == working_.end()) {
         co_return result;
      }

      auto current = family_found->second.lower_bound(request.after ? request.after->boundary : range.begin);
      if (request.after && current != family_found->second.end() && current->first == request.after->boundary) {
         ++current;
      }

      auto last = std::optional<forge::db::core::record_key>{};
      while (current != family_found->second.end()) {
         if (!starts_with(current->first, range.prefix)) {
            break;
         }
         if (range.has_end && !(current->first.bytes() < range.end.bytes())) {
            break;
         }
         result.entries.push_back(forge::db::core::record_entry{.key = current->first, .value = current->second});
         last = current->first;
         ++current;
         if (result.entries.size() == request.limit) {
            break;
         }
      }

      if (last && current != family_found->second.end() && starts_with(current->first, range.prefix) &&
          (!range.has_end || current->first.bytes() < range.end.bytes())) {
         result.next = forge::db::core::cursor{.boundary = *last};
      }
      co_return result;
   }

   boost::asio::awaitable<void> commit() override {
      if (state_->fail_commit) {
         throw std::runtime_error{"db test commit failure"};
      }
      if (writable_) {
         state_->records = std::move(working_);
      }
      co_return;
   }

   boost::asio::awaitable<void> rollback() override {
      ++state_->rollback_calls;
      co_return;
   }

 private:
   std::shared_ptr<memory_state> state_;
   family_map working_;
   bool snapshot_ = false;
   bool writable_ = false;
};

class memory_driver final : public forge::db::core::driver {
 public:
   explicit memory_driver(std::shared_ptr<memory_state> state) : state_{std::move(state)} {}

   boost::asio::awaitable<void> async_flush(bool) override {
      co_return;
   }

 private:
   boost::asio::awaitable<std::unique_ptr<forge::db::core::session>> open_transaction() override {
      co_return std::make_unique<memory_session>(state_, false, true);
   }

   boost::asio::awaitable<std::unique_ptr<forge::db::core::session>> open_snapshot() override {
      co_return std::make_unique<memory_session>(state_, true, false);
   }

   std::shared_ptr<memory_state> state_;
};

} // namespace

BOOST_AUTO_TEST_SUITE(db_test_suite)

BOOST_AUTO_TEST_CASE(db_transaction_commit_and_rollback_are_atomic) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto meta = forge::db::core::family{"meta"};

      {
         auto tx = co_await driver->begin_transaction();
         co_await tx.put(meta, key("a"), bytes("rollback"));
         co_await tx.rollback();
      }
      {
         auto read = co_await driver->begin_read();
         BOOST_CHECK(!(co_await read.get(meta, key("a"))).has_value());
      }

      auto tx = co_await driver->begin_transaction();
      co_await tx.put(meta, key("a"), bytes("commit"));
      co_await tx.commit();

      auto read = co_await driver->begin_read();
      const auto value = co_await read.get(meta, key("a"));
      BOOST_REQUIRE(value.has_value());
      BOOST_CHECK_EQUAL(text(*value), "commit");
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_snapshot_reads_preserve_precommit_state) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto meta = forge::db::core::family{"meta"};

      auto initial = co_await driver->begin_transaction();
      co_await initial.put(meta, key("a"), bytes("old"));
      co_await initial.commit();

      auto snapshot = co_await driver->begin_read();
      auto update = co_await driver->begin_transaction();
      co_await update.put(meta, key("a"), bytes("new"));
      co_await update.commit();

      BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(meta, key("a")))), "old");
      auto after = co_await driver->begin_read();
      BOOST_CHECK_EQUAL(text(*(co_await after.get(meta, key("a")))), "new");
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_scan_pages_use_opaque_cursor_boundaries) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto meta = forge::db::core::family{"meta"};
      auto tx = co_await driver->begin_transaction();
      co_await tx.put(meta, key("a"), bytes("1"));
      co_await tx.put(meta, key("b"), bytes("2"));
      co_await tx.put(meta, key("c"), bytes("3"));
      co_await tx.commit();

      auto snapshot = co_await driver->begin_read();
      auto first = co_await snapshot.scan_page(meta, forge::db::core::record_range{.begin = key("a"), .end = key("z")}, {.limit = 2});
      BOOST_REQUIRE_EQUAL(first.entries.size(), 2U);
      BOOST_REQUIRE(first.next.has_value());
      BOOST_CHECK_EQUAL(text(first.entries[0].value), "1");
      BOOST_CHECK_EQUAL(text(first.entries[1].value), "2");

      auto second = co_await snapshot.scan_page(
         meta,
         forge::db::core::record_range{.begin = key("a"), .end = key("z")},
         forge::db::core::page_request{.after = first.next, .limit = 2});
      BOOST_REQUIRE_EQUAL(second.entries.size(), 1U);
      BOOST_CHECK_EQUAL(text(second.entries[0].value), "3");
      BOOST_CHECK(!second.next.has_value());
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_transaction_participant_hooks_follow_commit_and_rollback) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto commits = 0U;
      auto rollbacks = 0U;
      {
         auto tx = co_await driver->begin_transaction();
         tx.after_commit([&]() -> boost::asio::awaitable<void> {
            ++commits;
            co_return;
         });
         tx.after_rollback([&]() -> boost::asio::awaitable<void> {
            ++rollbacks;
            co_return;
         });
         co_await tx.commit();
      }
      {
         auto tx = co_await driver->begin_transaction();
         tx.after_commit([&]() -> boost::asio::awaitable<void> {
            ++commits;
            co_return;
         });
         tx.after_rollback([&]() -> boost::asio::awaitable<void> {
            ++rollbacks;
            co_return;
         });
         co_await tx.rollback();
      }

      BOOST_CHECK_EQUAL(commits, 1U);
      BOOST_CHECK_EQUAL(rollbacks, 1U);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_transaction_awaits_async_rollback_hooks_before_returning) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto hook_completed = false;

      auto tx = co_await driver->begin_transaction();
      tx.after_rollback([&]() -> boost::asio::awaitable<void> {
         auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
         timer.expires_after(std::chrono::milliseconds{1});
         co_await timer.async_wait(boost::asio::use_awaitable);
         hook_completed = true;
         co_return;
      });

      co_await tx.rollback();
      BOOST_CHECK(hook_completed);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_commit_failure_preserves_rollback_state) {
   auto runtime = forge::asio::runtime{};
   auto state = std::make_shared<memory_state>();
   state->fail_commit = true;
   auto driver = std::make_shared<memory_driver>(state);

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto meta = forge::db::core::family{"meta"};
      auto rolled_back = false;

      auto tx = co_await driver->begin_transaction();
      tx.after_rollback([&]() -> boost::asio::awaitable<void> {
         rolled_back = true;
         co_return;
      });
      co_await tx.put(meta, key("a"), bytes("pending"));

      BOOST_CHECK_THROW(co_await tx.commit(), std::runtime_error);
      BOOST_CHECK(tx.active());

      co_await tx.rollback();
      BOOST_CHECK(!tx.active());
      BOOST_CHECK(rolled_back);
      BOOST_CHECK_EQUAL(state->rollback_calls, 1U);

      state->fail_commit = false;
      auto read = co_await driver->begin_read();
      BOOST_CHECK(!(co_await read.get(meta, key("a"))).has_value());
      co_return;
   }());
}

BOOST_AUTO_TEST_SUITE_END()
