#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import forge.asio.affine;
import forge.asio.blocking;
import forge.asio.exceptions;
import forge.asio.runtime;
import forge.db.core.driver;
import forge.db.core.exceptions;
import forge.db.core.record;
import forge.db.mdbx.driver;
import forge.db.mdbx.exceptions;

namespace {

std::filesystem::path make_test_root(std::string name) {
   static auto sequence = std::atomic_uint64_t{0};
   const auto nonce = sequence.fetch_add(1, std::memory_order_relaxed);
   auto root = std::filesystem::temp_directory_path() /
               (std::move(name) + "_" + std::to_string(nonce));
   std::filesystem::remove_all(root);
   return root;
}

std::vector<std::byte> bytes(std::string value) {
   return {
      reinterpret_cast<const std::byte*>(value.data()),
      reinterpret_cast<const std::byte*>(value.data() + value.size()),
   };
}

std::string text(const std::vector<std::byte>& value) {
   return {
      reinterpret_cast<const char*>(value.data()),
      reinterpret_cast<const char*>(value.data() + value.size()),
   };
}

forge::db::core::record_key key(std::string value) {
   return forge::db::core::record_key{bytes(std::move(value))};
}

forge::db::core::record_key empty_key() {
   return forge::db::core::record_key{std::vector<std::byte>{}};
}

forge::db::mdbx::config config_for(const std::filesystem::path& path) {
   return forge::db::mdbx::config{
      .path = path.string(),
      .families = {"objectdb", "blobdb.data", "blobdb.refs", "records"},
   };
}

boost::asio::awaitable<std::shared_ptr<forge::db::mdbx::driver>>
open_driver(const std::filesystem::path& path,
            const forge::asio::affine::lane& lane) {
   co_return co_await forge::db::mdbx::driver::open(config_for(path),
                                                    lane.get_executor());
}

} // namespace

BOOST_AUTO_TEST_SUITE(db_mdbx_test_suite)

BOOST_AUTO_TEST_CASE(db_mdbx_transactions_are_atomic_and_persist_across_reopen) {
   const auto root = make_test_root("forge_db_mdbx_atomic");
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{{.thread_name = "mdbx-test"}};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await open_driver(root / "store", lane);
      const auto objects = forge::db::core::family{"objectdb"};
      const auto blobs = forge::db::core::family{"blobdb.data"};
      const auto refs = forge::db::core::family{"blobdb.refs"};

      {
         auto transaction = co_await driver->begin_transaction();
         co_await transaction.put(objects, key("doc:1"), bytes("discarded"));
         co_await transaction.put(blobs, key("blob:1"), bytes("discarded"));
         co_await transaction.rollback();
      }
      {
         auto snapshot = co_await driver->begin_read();
         BOOST_CHECK(!(co_await snapshot.get(objects, key("doc:1"))).has_value());
         BOOST_CHECK(!(co_await snapshot.get(blobs, key("blob:1"))).has_value());
      }

      {
         auto transaction = co_await driver->begin_transaction();
         co_await transaction.put(objects, key("doc:1"), bytes("metadata"));
         co_await transaction.put(blobs, key("blob:1"), bytes("payload"));
         co_await transaction.put(refs, key("ref:1"), bytes("doc:1"));
         co_await transaction.commit();
      }
      co_await driver->async_flush(true);
      co_await driver->async_close();
      driver.reset();

      auto reopened = co_await open_driver(root / "store", lane);
      {
         auto snapshot = co_await reopened->begin_read();
         BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(objects, key("doc:1")))),
                           "metadata");
         BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(blobs, key("blob:1")))),
                           "payload");
         BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(refs, key("ref:1")))),
                           "doc:1");
      }
      co_await reopened->async_close();
      reopened.reset();
      co_await lane.shutdown();
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_nested_savepoints_merge_and_rollback_without_closing_outer) {
   const auto root = make_test_root("forge_db_mdbx_savepoints");
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await open_driver(root / "store", lane);
      const auto objects = forge::db::core::family{"objectdb"};
      const auto blobs = forge::db::core::family{"blobdb.data"};

      {
         auto transaction = co_await driver->begin_transaction();
         co_await transaction.put(objects, key("before"), bytes("kept"));
         const auto outer = co_await transaction.create_savepoint();
         co_await transaction.put(objects, key("merged"), bytes("kept"));
         const auto inner = co_await transaction.create_savepoint();
         co_await transaction.put(blobs, key("discarded"), bytes("no"));
         co_await transaction.rollback_to_savepoint(inner);
         co_await transaction.release_savepoint(outer);
         co_await transaction.put(objects, key("after"), bytes("kept"));
         co_await transaction.commit();
      }

      {
         auto snapshot = co_await driver->begin_read();
         BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(objects, key("before")))), "kept");
         BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(objects, key("merged")))), "kept");
         BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(objects, key("after")))), "kept");
         BOOST_CHECK(!(co_await snapshot.get(blobs, key("discarded"))).has_value());
      }
      co_await driver->async_close();
      driver.reset();
      co_await lane.shutdown();
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_snapshot_preserves_old_state_and_scan_contract) {
   const auto root = make_test_root("forge_db_mdbx_snapshot");
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await open_driver(root / "store", lane);
      const auto objects = forge::db::core::family{"objectdb"};
      {
         auto seed = co_await driver->begin_transaction();
         co_await seed.put(objects, empty_key(), bytes("empty"));
         for (const auto* value : {"a", "aa", "b", "y", "z"}) {
            co_await seed.put(objects, key(value), bytes(value));
         }
         co_await seed.commit();
      }

      auto snapshot = co_await driver->begin_read();
      {
         auto update = co_await driver->begin_transaction();
         co_await update.put(objects, key("a"), bytes("new"));
         co_await update.erase(objects, key("aa"));
         co_await update.commit();
      }

      BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(objects, key("a")))), "a");
      auto first = co_await snapshot.scan_page(
         objects,
         forge::db::core::record_range{.begin = empty_key(), .end = key("z")},
         forge::db::core::page_request{.limit = 1});
      BOOST_REQUIRE_EQUAL(first.entries.size(), 1U);
      BOOST_CHECK(first.entries.front().key.empty());
      BOOST_REQUIRE(first.next.has_value());
      BOOST_CHECK(first.next->boundary.empty());

      auto second = co_await snapshot.scan_page(
         objects,
         forge::db::core::record_range{.begin = empty_key(), .end = key("z")},
         forge::db::core::page_request{.after = first.next, .limit = 3});
      BOOST_REQUIRE_EQUAL(second.entries.size(), 3U);
      BOOST_CHECK_EQUAL(text(second.entries[0].value), "a");
      BOOST_CHECK_EQUAL(text(second.entries[1].value), "aa");
      BOOST_CHECK_EQUAL(text(second.entries[2].value), "b");
      BOOST_REQUIRE(second.next.has_value());

      auto bounded = co_await snapshot.scan_page(
         objects,
         forge::db::core::record_range{.begin = key("a"), .end = key("b")},
         forge::db::core::page_request{.limit = 10});
      BOOST_REQUIRE_EQUAL(bounded.entries.size(), 2U);
      BOOST_CHECK(!bounded.next.has_value());

      snapshot = {};
      co_await driver->async_close();
      driver.reset();
      co_await lane.shutdown();
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_dropped_transaction_releases_writer_after_native_abort) {
   const auto root = make_test_root("forge_db_mdbx_drop");
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await open_driver(root / "store", lane);
      const auto objects = forge::db::core::family{"objectdb"};
      {
         auto dropped = co_await driver->begin_transaction();
         co_await dropped.put(objects, key("dropped"), bytes("not committed"));
      }

      {
         auto successor = co_await driver->begin_transaction();
         BOOST_CHECK(!(co_await successor.get(objects, key("dropped"))).has_value());
         co_await successor.put(objects, key("successor"), bytes("committed"));
         co_await successor.commit();
      }
      {
         auto snapshot = co_await driver->begin_read();
         BOOST_CHECK(!(co_await snapshot.get(objects, key("dropped"))).has_value());
         BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(objects, key("successor")))),
                           "committed");
      }
      co_await driver->async_close();
      driver.reset();
      co_await lane.shutdown();
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_async_close_is_fail_fast_and_retryable) {
   const auto root = make_test_root("forge_db_mdbx_close");
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await open_driver(root / "store", lane);
      auto transaction = co_await driver->begin_transaction();

      auto busy = false;
      try {
         co_await driver->async_close();
      } catch (const forge::db::core::exceptions::driver_busy&) {
         busy = true;
      }
      BOOST_CHECK(busy);

      auto closed = false;
      try {
         static_cast<void>(co_await driver->begin_read());
      } catch (const forge::db::core::exceptions::driver_closed&) {
         closed = true;
      }
      BOOST_CHECK(closed);

      co_await transaction.rollback();
      transaction = {};
      co_await driver->async_close();
      co_await driver->async_close();
      driver.reset();
      co_await lane.shutdown();
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_snapshot_copies_support_parallel_reads) {
   const auto root = make_test_root("forge_db_mdbx_parallel_snapshot");
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};
   auto driver = forge::asio::blocking::run(
      runtime, open_driver(root / "store", lane));

   const auto objects = forge::db::core::family{"objectdb"};
   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto seed = co_await driver->begin_transaction();
      for (auto index = 0; index < 64; ++index) {
         co_await seed.put(objects, key("key:" + std::to_string(index)),
                           bytes("value:" + std::to_string(index)));
      }
      co_await seed.commit();
   }());

   auto snapshot = forge::asio::blocking::run(runtime, driver->begin_read());
   auto succeeded = std::atomic_size_t{0};
   auto readers = std::vector<std::thread>{};
   for (auto thread_index = 0; thread_index < 8; ++thread_index) {
      readers.emplace_back([snapshot, objects, thread_index, &succeeded]() mutable {
         auto reader_runtime = forge::asio::runtime{};
         forge::asio::blocking::run(
            reader_runtime,
            [snapshot = std::move(snapshot), objects, thread_index,
             &succeeded]() mutable -> boost::asio::awaitable<void> {
               for (auto offset = 0; offset < 8; ++offset) {
                  const auto index = thread_index * 8 + offset;
                  const auto loaded = co_await snapshot.get(
                     objects, key("key:" + std::to_string(index)));
                  BOOST_REQUIRE(loaded.has_value());
                  BOOST_CHECK_EQUAL(text(*loaded), "value:" + std::to_string(index));
                  succeeded.fetch_add(1, std::memory_order_relaxed);
               }
            }());
      });
   }
   for (auto& reader : readers) {
      reader.join();
   }
   BOOST_CHECK_EQUAL(succeeded.load(std::memory_order_relaxed), 64U);

   snapshot = {};
   forge::asio::blocking::run(runtime, driver->async_close());
   driver.reset();
   forge::asio::blocking::run(runtime, lane.shutdown());
   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_snapshot_keeps_native_environment_alive_after_driver_drop) {
   const auto root = make_test_root("forge_db_mdbx_snapshot_owner");
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = co_await open_driver(root / "store", lane);
      const auto objects = forge::db::core::family{"objectdb"};
      {
         auto seed = co_await driver->begin_transaction();
         co_await seed.put(objects, key("key"), bytes("value"));
         co_await seed.commit();
      }

      auto snapshot = co_await driver->begin_read();
      driver.reset();
      BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(objects, key("key")))),
                        "value");

      snapshot = {};
      auto reopened = co_await open_driver(root / "store", lane);
      co_await reopened->async_close();
      reopened.reset();
      co_await lane.shutdown();
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_rejects_invalid_config_and_unknown_family) {
   const auto root = make_test_root("forge_db_mdbx_config");
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto invalid = config_for(root / "invalid");
      invalid.families.push_back("objectdb");
      auto rejected = false;
      try {
         static_cast<void>(co_await forge::db::mdbx::driver::open(
            std::move(invalid), lane.get_executor()));
      } catch (const forge::db::mdbx::exceptions::invalid_config&) {
         rejected = true;
      }
      BOOST_CHECK(rejected);

      auto driver = co_await open_driver(root / "store", lane);
      auto transaction = co_await driver->begin_transaction();
      auto unknown = false;
      try {
         static_cast<void>(co_await transaction.get(
            forge::db::core::family{"unknown"}, key("key")));
      } catch (const forge::db::mdbx::exceptions::invalid_family&) {
         unknown = true;
      }
      BOOST_CHECK(unknown);
      co_await transaction.rollback();
      transaction = {};
      co_await driver->async_close();
      driver.reset();
      co_await lane.shutdown();
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_writer_admission_is_fifo_and_cancellable) {
   const auto root = make_test_root("forge_db_mdbx_writer_admission");
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto lane = forge::asio::affine::lane{};
   auto driver = forge::asio::blocking::run(runtime, open_driver(root / "store", lane));
   auto owner = forge::asio::blocking::run(runtime, driver->begin_transaction());
   auto order = std::vector<int>{};
   auto order_mutex = std::mutex{};

   const auto writer = [&](int ordinal) -> boost::asio::awaitable<void> {
      auto transaction = co_await driver->begin_transaction();
      {
         const auto lock = std::scoped_lock{order_mutex};
         order.push_back(ordinal);
      }
      co_await transaction.rollback();
   };

   auto first = boost::asio::co_spawn(runtime.context(), writer(1), boost::asio::use_future);
   std::this_thread::sleep_for(std::chrono::milliseconds{10});
   auto canceled_signal = boost::asio::cancellation_signal{};
   auto canceled = boost::asio::co_spawn(
      runtime.context(), driver->begin_transaction(),
      boost::asio::bind_cancellation_slot(canceled_signal.slot(),
                                           boost::asio::use_future));
   std::this_thread::sleep_for(std::chrono::milliseconds{10});
   auto second = boost::asio::co_spawn(runtime.context(), writer(2), boost::asio::use_future);
   std::this_thread::sleep_for(std::chrono::milliseconds{20});

   canceled_signal.emit(boost::asio::cancellation_type::all);
   BOOST_CHECK_THROW(static_cast<void>(canceled.get()),
                     forge::asio::exceptions::canceled);
   forge::asio::blocking::run(runtime, owner.rollback());
   first.get();
   second.get();

   const auto expected = std::vector<int>{1, 2};
   BOOST_CHECK_EQUAL_COLLECTIONS(order.begin(), order.end(),
                                 expected.begin(), expected.end());

   auto successor = forge::asio::blocking::run(runtime, driver->begin_transaction());
   forge::asio::blocking::run(runtime, successor.rollback());
   forge::asio::blocking::run(runtime, driver->async_close());
   driver.reset();
   forge::asio::blocking::run(runtime, lane.shutdown());
   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_flush_waits_for_the_active_writer) {
   const auto root = make_test_root("forge_db_mdbx_flush_writer");
   auto runtime = forge::asio::runtime{forge::asio::runtime_options{.worker_threads = 2}};
   auto lane = forge::asio::affine::lane{};
   auto driver = forge::asio::blocking::run(runtime, open_driver(root / "store", lane));
   auto active = forge::asio::blocking::run(runtime, driver->begin_transaction());

   auto flushed = boost::asio::co_spawn(
      runtime.context(), driver->async_flush(true), boost::asio::use_future);
   BOOST_CHECK(flushed.wait_for(std::chrono::milliseconds{20}) ==
               std::future_status::timeout);

   forge::asio::blocking::run(runtime, active.rollback());
   flushed.get();
   forge::asio::blocking::run(runtime, driver->async_close());
   driver.reset();
   forge::asio::blocking::run(runtime, lane.shutdown());
   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_exclusive_open_and_native_limits_are_typed) {
   const auto root = make_test_root("forge_db_mdbx_limits");
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto limited = config_for(root / "store");
      limited.max_readers = 2;
      auto driver = co_await forge::db::mdbx::driver::open(
         limited, lane.get_executor());

      BOOST_CHECK_THROW(
         co_await forge::db::mdbx::driver::open(limited, lane.get_executor()),
         forge::db::mdbx::exceptions::environment_busy);

      auto readers = std::vector<forge::db::core::snapshot>{};
      auto readers_full = false;
      for (auto ordinal = 0; ordinal < 1024 && !readers_full; ++ordinal) {
         try {
            readers.push_back(co_await driver->begin_read());
         } catch (const forge::db::mdbx::exceptions::readers_full&) {
            readers_full = true;
         }
      }
      BOOST_REQUIRE(readers_full);
      BOOST_REQUIRE(!readers.empty());
      readers.back() = {};
      auto replacement = co_await driver->begin_read();
      readers.clear();
      replacement = {};

      auto transaction = co_await driver->begin_transaction();
      auto oversized = forge::db::core::record_key{
         std::vector<std::byte>(1024U * 1024U, std::byte{0x2a})};
      BOOST_CHECK_THROW(
         co_await transaction.put(forge::db::core::family{"objectdb"},
                                  std::move(oversized), bytes("value")),
         forge::db::mdbx::exceptions::key_too_large);
      co_await transaction.rollback();
      transaction = {};

      co_await driver->async_close();
      driver.reset();
      co_await lane.shutdown();
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_map_full_is_typed_and_does_not_commit_partial_data) {
   const auto root = make_test_root("forge_db_mdbx_map_full");
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto limited = config_for(root / "store");
      limited.map.lower_size = 1024U * 1024U;
      limited.map.current_size = 1024U * 1024U;
      limited.map.upper_size = 1024U * 1024U;
      auto driver = co_await forge::db::mdbx::driver::open(
         limited, lane.get_executor());
      auto transaction = co_await driver->begin_transaction();
      const auto payload = std::vector<std::byte>(64U * 1024U, std::byte{0x5a});
      auto full = false;
      for (auto ordinal = 0; ordinal < 64 && !full; ++ordinal) {
         try {
            co_await transaction.put(
               forge::db::core::family{"objectdb"},
               key("large:" + std::to_string(ordinal)), payload);
         } catch (const forge::db::mdbx::exceptions::map_full&) {
            full = true;
         }
      }
      BOOST_REQUIRE(full);
      co_await transaction.rollback();
      transaction = {};

      auto snapshot = co_await driver->begin_read();
      BOOST_CHECK(!(co_await snapshot.get(
         forge::db::core::family{"objectdb"}, key("large:0"))).has_value());
      snapshot = {};
      co_await driver->async_close();
      driver.reset();
      co_await lane.shutdown();
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_mdbx_safe_nosync_reopens_a_valid_committed_prefix) {
   const auto root = make_test_root("forge_db_mdbx_safe_nosync");
   auto runtime = forge::asio::runtime{};
   auto lane = forge::asio::affine::lane{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto value = config_for(root / "store");
      value.durability_mode = forge::db::mdbx::durability::safe_nosync;
      auto driver = co_await forge::db::mdbx::driver::open(value,
                                                           lane.get_executor());
      for (auto ordinal = 0; ordinal < 8; ++ordinal) {
         auto transaction = co_await driver->begin_transaction();
         co_await transaction.put(
            forge::db::core::family{"objectdb"},
            key("commit:" + std::to_string(ordinal)), bytes(std::to_string(ordinal)));
         co_await transaction.commit();
      }
      co_await driver->async_close();
      driver.reset();

      auto reopened = co_await forge::db::mdbx::driver::open(value,
                                                             lane.get_executor());
      auto snapshot = co_await reopened->begin_read();
      for (auto ordinal = 0; ordinal < 8; ++ordinal) {
         const auto stored = co_await snapshot.get(
            forge::db::core::family{"objectdb"},
            key("commit:" + std::to_string(ordinal)));
         BOOST_REQUIRE(stored.has_value());
         BOOST_CHECK_EQUAL(text(*stored), std::to_string(ordinal));
      }
      snapshot = {};
      co_await reopened->async_close();
      reopened.reset();
      co_await lane.shutdown();
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_SUITE_END()
