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
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.core.driver;
import forge.db.core.exceptions;
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
   bool fail_rollback = false;
   std::size_t rollback_calls = 0;
   std::size_t destroyed_sessions = 0;
   bool support_savepoints = true;
   bool support_record_locks = true;
};

class memory_session final : public forge::db::core::session {
 public:
   memory_session(std::shared_ptr<memory_state> state, bool snapshot, bool writable)
       : state_{std::move(state)}, working_{state_->records}, snapshot_{snapshot}, writable_{writable} {}

   ~memory_session() override {
      ++state_->destroyed_sessions;
   }

   [[nodiscard]] forge::db::core::capabilities capabilities() const noexcept override {
      return forge::db::core::capabilities{
         .snapshot_reads = snapshot_,
         .writes = writable_,
         .savepoints = writable_ && state_->support_savepoints,
         .record_locks = writable_ && state_->support_record_locks,
      };
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

   boost::asio::awaitable<std::optional<std::vector<std::byte>>>
   get_for_update(forge::db::core::family family, forge::db::core::record_key record) override {
      co_return co_await get(std::move(family), std::move(record));
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

   boost::asio::awaitable<void> create_savepoint() override {
      savepoints_.push_back(working_);
      co_return;
   }

   boost::asio::awaitable<void> rollback_to_savepoint() override {
      if (savepoints_.empty()) {
         throw std::logic_error{"db test savepoint stack is empty"};
      }
      working_ = std::move(savepoints_.back());
      savepoints_.pop_back();
      co_return;
   }

   boost::asio::awaitable<void> release_savepoint() override {
      if (savepoints_.empty()) {
         throw std::logic_error{"db test savepoint stack is empty"};
      }
      savepoints_.pop_back();
      co_return;
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
      if (state_->fail_rollback) {
         throw std::runtime_error{"db test rollback failure"};
      }
      co_return;
   }

 private:
   std::shared_ptr<memory_state> state_;
   family_map working_;
   std::vector<family_map> savepoints_;
   bool snapshot_ = false;
   bool writable_ = false;
};

class tracking_participant final : public forge::db::core::transaction_participant {
 public:
   [[nodiscard]] std::string_view name() const noexcept override {
      return "db-test-tracking";
   }

   [[nodiscard]] bool captures_mutations() const noexcept override {
      return true;
   }

   boost::asio::awaitable<void> prepare_mutation(const forge::db::core::record_mutation& mutation) override {
      pending_ = mutation;
      co_return;
   }

   void publish_mutation() noexcept override {
      captured_.push_back(std::move(*pending_));
      pending_.reset();
   }

   void discard_mutation() noexcept override {
      pending_.reset();
   }

   boost::asio::awaitable<void> prepare_savepoint(forge::db::core::savepoint_id_t) override {
      pending_frame_ = captured_.size();
      co_return;
   }

   void publish_savepoint(forge::db::core::savepoint_id_t) noexcept override {
      frames_.push_back(*pending_frame_);
      pending_frame_.reset();
   }

   void discard_savepoint(forge::db::core::savepoint_id_t) noexcept override {
      pending_frame_.reset();
   }

   boost::asio::awaitable<void>
   rollback_to_savepoint(forge::db::core::savepoint_id_t, forge::db::core::participant_access&) override {
      captured_.resize(frames_.back());
      frames_.pop_back();
      if (fail_restore) {
         throw std::runtime_error{"db test participant restore failure"};
      }
      co_return;
   }

   boost::asio::awaitable<void>
   release_savepoint(forge::db::core::savepoint_id_t, forge::db::core::participant_access&) override {
      frames_.pop_back();
      co_return;
   }

   boost::asio::awaitable<void> prepare_commit(forge::db::core::participant_access&) override {
      prepared = true;
      co_return;
   }

   [[nodiscard]] std::size_t captured() const noexcept {
      return captured_.size();
   }

   bool fail_restore = false;
   bool prepared = false;

 private:
   std::optional<forge::db::core::record_mutation> pending_;
   std::optional<std::size_t> pending_frame_;
   std::vector<std::size_t> frames_;
   std::vector<forge::db::core::record_mutation> captured_;
};

class claiming_participant final : public forge::db::core::transaction_participant {
 public:
   explicit claiming_participant(std::string name,
                                 std::vector<forge::db::core::family> families = {})
       : name_{std::move(name)}, families_{std::move(families)} {}

   [[nodiscard]] std::string_view name() const noexcept override {
      return name_;
   }

   [[nodiscard]] std::span<const forge::db::core::family>
   exclusive_families() const noexcept override {
      return families_;
   }

 private:
   std::string name_;
   std::vector<forge::db::core::family> families_;
};

class unclaimed_participant final : public forge::db::core::transaction_participant {
 public:
   explicit unclaimed_participant(std::string name) : name_{std::move(name)} {}

   [[nodiscard]] std::string_view name() const noexcept override {
      return name_;
   }

 private:
   std::string name_;
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

BOOST_AUTO_TEST_CASE(db_transaction_savepoint_rolls_back_suffix_and_remains_active) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto meta = forge::db::core::family{"meta"};
      auto tx = co_await driver->begin_transaction();
      co_await tx.put(meta, key("a"), bytes("kept"));
      const auto point = co_await tx.create_savepoint();
      co_await tx.put(meta, key("b"), bytes("discarded"));

      co_await tx.rollback_to_savepoint(point);
      BOOST_CHECK(tx.active());
      BOOST_CHECK(!(co_await tx.get(meta, key("b"))).has_value());
      co_await tx.put(meta, key("c"), bytes("continued"));
      co_await tx.commit();

      auto read = co_await driver->begin_read();
      BOOST_CHECK_EQUAL(text(*(co_await read.get(meta, key("a")))), "kept");
      BOOST_CHECK(!(co_await read.get(meta, key("b"))).has_value());
      BOOST_CHECK_EQUAL(text(*(co_await read.get(meta, key("c")))), "continued");
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_transaction_nested_savepoints_enforce_lifo_and_release_semantics) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto meta = forge::db::core::family{"meta"};
      auto tx = co_await driver->begin_transaction();
      const auto outer = co_await tx.create_savepoint();
      co_await tx.put(meta, key("a"), bytes("outer"));
      const auto inner = co_await tx.create_savepoint();
      co_await tx.put(meta, key("b"), bytes("inner"));

      BOOST_CHECK_THROW(co_await tx.rollback_to_savepoint(outer), forge::db::core::exceptions::invalid_savepoint);
      co_await tx.release_savepoint(inner);
      BOOST_CHECK_THROW(co_await tx.release_savepoint(inner), forge::db::core::exceptions::invalid_savepoint);
      co_await tx.rollback_to_savepoint(outer);
      co_await tx.commit();

      auto read = co_await driver->begin_read();
      BOOST_CHECK(!(co_await read.get(meta, key("a"))).has_value());
      BOOST_CHECK(!(co_await read.get(meta, key("b"))).has_value());
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_transaction_savepoint_restores_participant_and_prepares_commit) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto meta = forge::db::core::family{"meta"};
      auto participant = std::make_shared<tracking_participant>();
      auto tx = co_await driver->begin_transaction();
      tx.attach_participant(participant);
      co_await tx.put(meta, key("a"), bytes("kept"));
      const auto point = co_await tx.create_savepoint();
      co_await tx.put(meta, key("b"), bytes("discarded"));
      BOOST_CHECK_EQUAL(participant->captured(), 2U);

      co_await tx.rollback_to_savepoint(point);
      BOOST_CHECK_EQUAL(participant->captured(), 1U);
      co_await tx.commit();
      BOOST_CHECK(participant->prepared);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_transaction_participants_reject_overlapping_exclusive_families) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto tx = co_await driver->begin_transaction();
      tx.attach_participant(std::make_shared<claiming_participant>(
         "first", std::vector{forge::db::core::family{"one"}, forge::db::core::family{"shared"}}));
      tx.attach_participant(std::make_shared<claiming_participant>(
         "independent", std::vector{forge::db::core::family{"two"}}));

      try {
         tx.attach_participant(std::make_shared<claiming_participant>(
            "overlapping", std::vector{forge::db::core::family{"shared"}}));
         BOOST_FAIL("overlapping participant family was accepted");
      } catch (const forge::db::core::exceptions::participant_conflict& error) {
         const auto context_value = [&error](std::string_view key_value) -> std::string_view {
            const auto& context = error.context();
            const auto field = std::find_if(context.begin(), context.end(), [&](const auto& value) {
               return value.key == key_value;
            });
            return field == context.end() ? std::string_view{} : std::string_view{field->value};
         };
         BOOST_CHECK_EQUAL(context_value("family"), "shared");
         BOOST_CHECK_EQUAL(context_value("participant"), "overlapping");
         BOOST_CHECK_EQUAL(context_value("existing-participant"), "first");
      }

      tx.attach_participant(std::make_shared<claiming_participant>(
         "still-independent", std::vector{forge::db::core::family{"three"}}));
      co_await tx.rollback();
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_transaction_participant_claims_preserve_default_and_name_duplicate_behavior) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto tx = co_await driver->begin_transaction();
      tx.attach_participant(std::make_shared<unclaimed_participant>("unclaimed-first"));
      tx.attach_participant(std::make_shared<unclaimed_participant>("unclaimed-second"));

      BOOST_CHECK_THROW(
         tx.attach_participant(std::make_shared<unclaimed_participant>("unclaimed-first")),
         forge::db::core::exceptions::participant_conflict);
      co_await tx.rollback();
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_transaction_reports_claimed_families) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto claimed = forge::db::core::family{"claimed"};
      auto tx = co_await driver->begin_transaction();
      BOOST_CHECK(!tx.claims_family(claimed));

      tx.attach_participant(std::make_shared<claiming_participant>(
         "owner", std::vector{claimed}));
      BOOST_CHECK(tx.claims_family(claimed));
      BOOST_CHECK(!tx.claims_family(forge::db::core::family{"other"}));

      co_await tx.rollback();
      BOOST_CHECK(!tx.claims_family(claimed));
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_transaction_participant_restore_failure_marks_rollback_only) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto meta = forge::db::core::family{"meta"};
      auto participant = std::make_shared<tracking_participant>();
      auto tx = co_await driver->begin_transaction();
      tx.attach_participant(participant);
      const auto point = co_await tx.create_savepoint();
      co_await tx.put(meta, key("a"), bytes("discarded"));
      participant->fail_restore = true;

      BOOST_CHECK_THROW(co_await tx.rollback_to_savepoint(point), std::runtime_error);
      BOOST_CHECK_THROW(co_await tx.put(meta, key("b"), bytes("rejected")),
                        forge::db::core::exceptions::transaction_rollback_only);
      BOOST_CHECK_THROW(co_await tx.commit(), forge::db::core::exceptions::transaction_rollback_only);
      co_await tx.rollback();
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_transaction_savepoint_requires_backend_capability) {
   auto runtime = forge::asio::runtime{};
   auto state = std::make_shared<memory_state>();
   state->support_savepoints = false;
   auto driver = std::make_shared<memory_driver>(state);

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto tx = co_await driver->begin_transaction();
      BOOST_CHECK_THROW(co_await tx.create_savepoint(), forge::db::core::exceptions::unsupported_operation);
      co_await tx.rollback();
      co_return;
   }());
}

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

BOOST_AUTO_TEST_CASE(db_commit_hook_failure_keeps_commit_boundary_closed) {
   auto runtime = forge::asio::runtime{};
   auto state = std::make_shared<memory_state>();
   auto driver = std::make_shared<memory_driver>(state);

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto meta = forge::db::core::family{"meta"};
      auto rollback_hook_called = false;
      auto session_destroyed_before_commit_hook = false;

      auto tx = co_await driver->begin_transaction();
      co_await tx.put(meta, key("a"), bytes("committed"));
      tx.after_commit([&]() -> boost::asio::awaitable<void> {
         session_destroyed_before_commit_hook = state->destroyed_sessions == 1U;
         throw std::runtime_error{"db test commit hook failure"};
         co_return;
      });
      tx.after_rollback([&]() -> boost::asio::awaitable<void> {
         rollback_hook_called = true;
         co_return;
      });

      BOOST_CHECK_THROW(co_await tx.commit(), std::runtime_error);
      BOOST_CHECK(!tx.active());
      co_await tx.rollback();

      auto read = co_await driver->begin_read();
      const auto value = co_await read.get(meta, key("a"));
      BOOST_REQUIRE(value.has_value());
      BOOST_CHECK_EQUAL(text(*value), "committed");
      BOOST_CHECK(session_destroyed_before_commit_hook);
      BOOST_CHECK(!rollback_hook_called);
      BOOST_CHECK_EQUAL(state->rollback_calls, 0U);
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

BOOST_AUTO_TEST_CASE(db_dropped_transaction_swallows_rollback_hook_failure) {
   auto runtime = forge::asio::runtime{};
   auto state = std::make_shared<memory_state>();
   auto driver = std::make_shared<memory_driver>(state);

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto hook_called = false;

      {
         auto tx = co_await driver->begin_transaction();
         tx.after_rollback([&]() -> boost::asio::awaitable<void> {
            hook_called = true;
            throw std::runtime_error{"db test rollback hook failure"};
            co_return;
         });
      }

      auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
      for (auto attempt = 0; attempt != 100 && !hook_called; ++attempt) {
         timer.expires_after(std::chrono::milliseconds{1});
         co_await timer.async_wait(boost::asio::use_awaitable);
      }

      BOOST_CHECK(hook_called);
      BOOST_CHECK_EQUAL(state->rollback_calls, 1U);
      BOOST_CHECK_EQUAL(state->destroyed_sessions, 1U);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_dropped_transaction_runs_rollback_hooks_after_backend_rollback_failure) {
   auto runtime = forge::asio::runtime{};
   auto state = std::make_shared<memory_state>();
   state->fail_rollback = true;
   auto driver = std::make_shared<memory_driver>(state);

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto hook_called = false;

      {
         auto tx = co_await driver->begin_transaction();
         tx.after_rollback([&]() -> boost::asio::awaitable<void> {
            hook_called = true;
            co_return;
         });
      }

      auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
      for (auto attempt = 0; attempt != 100 && !hook_called; ++attempt) {
         timer.expires_after(std::chrono::milliseconds{1});
         co_await timer.async_wait(boost::asio::use_awaitable);
      }

      BOOST_CHECK(hook_called);
      BOOST_CHECK_EQUAL(state->rollback_calls, 1U);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_dropped_transaction_destroys_session_before_rollback_hooks) {
   auto runtime = forge::asio::runtime{};
   auto state = std::make_shared<memory_state>();
   auto driver = std::make_shared<memory_driver>(state);

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto hook_called = false;
      auto session_destroyed_before_hook = false;

      {
         auto tx = co_await driver->begin_transaction();
         tx.after_rollback([&]() -> boost::asio::awaitable<void> {
            hook_called = true;
            session_destroyed_before_hook = state->destroyed_sessions == 1U;
            co_return;
         });
      }

      auto timer = boost::asio::steady_timer{co_await boost::asio::this_coro::executor};
      for (auto attempt = 0; attempt != 100 && !hook_called; ++attempt) {
         timer.expires_after(std::chrono::milliseconds{1});
         co_await timer.async_wait(boost::asio::use_awaitable);
      }

      BOOST_REQUIRE(hook_called);
      BOOST_CHECK(session_destroyed_before_hook);
      BOOST_CHECK_EQUAL(state->destroyed_sessions, 1U);
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
