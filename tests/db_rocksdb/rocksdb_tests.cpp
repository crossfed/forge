#include <boost/asio/awaitable.hpp>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.driver;
import forge.db.record;
import forge.db.rocksdb;
import forge.rocksdb.types;

namespace {

std::filesystem::path make_test_root(std::string name) {
   const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
   auto root = std::filesystem::temp_directory_path() / (std::move(name) + "_" + std::to_string(nonce));
   std::filesystem::remove_all(root);
   return root;
}

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

forge::db::record_key key(std::string text_value) {
   return forge::db::record_key{bytes(std::move(text_value))};
}

forge::db::record_key empty_key() {
   return forge::db::record_key{std::vector<std::byte>{}};
}

forge::db::rocksdb::config config_for(const std::filesystem::path& path) {
   auto blob_family = forge::rocksdb::column_family_config{"blobdb.data"};
   blob_family.blobs.enable_blob_files = true;
   blob_family.blobs.min_blob_size = 16;
   blob_family.blobs.blob_file_size = 1024 * 1024;

   return forge::db::rocksdb::config{
      .path = path.string(),
      .families = {
         forge::rocksdb::column_family_config{"objectdb"},
         std::move(blob_family),
         forge::rocksdb::column_family_config{"blobdb.refs"},
      },
   };
}

} // namespace

BOOST_AUTO_TEST_SUITE(db_rocksdb_test_suite)

BOOST_AUTO_TEST_CASE(db_rocksdb_transaction_writes_across_families_atomically) {
   const auto root = make_test_root("forge_db_rocksdb_atomic");
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = forge::db::rocksdb::driver{config_for(root / "store")};
      const auto objects = forge::db::family{"objectdb"};
      const auto blobs = forge::db::family{"blobdb.data"};
      const auto refs = forge::db::family{"blobdb.refs"};

      {
         auto tx = co_await driver.begin_transaction();
         co_await tx.put(objects, key("doc:1"), bytes("metadata"));
         co_await tx.put(blobs, key("blob:1"), bytes("payload"));
         co_await tx.put(refs, key("ref:1"), bytes("doc:1"));
         co_await tx.rollback();
      }

      {
         auto snapshot = co_await driver.begin_read();
         BOOST_CHECK(!(co_await snapshot.get(objects, key("doc:1"))).has_value());
         BOOST_CHECK(!(co_await snapshot.get(blobs, key("blob:1"))).has_value());
      }

      auto tx = co_await driver.begin_transaction();
      co_await tx.put(objects, key("doc:1"), bytes("metadata"));
      co_await tx.put(blobs, key("blob:1"), bytes("payload"));
      co_await tx.put(refs, key("ref:1"), bytes("doc:1"));
      co_await tx.commit();
      driver.flush(true);

      co_return;
   }());

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto reopened = forge::db::rocksdb::driver{config_for(root / "store")};
      auto snapshot = co_await reopened.begin_read();
      BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(forge::db::family{"objectdb"}, key("doc:1")))), "metadata");
      BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(forge::db::family{"blobdb.data"}, key("blob:1")))), "payload");
      BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(forge::db::family{"blobdb.refs"}, key("ref:1")))), "doc:1");
      co_return;
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_rocksdb_snapshot_preserves_old_records_and_scan_pages) {
   const auto root = make_test_root("forge_db_rocksdb_snapshot");
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = forge::db::rocksdb::driver{config_for(root / "store")};
      const auto objects = forge::db::family{"objectdb"};

      auto seed = co_await driver.begin_transaction();
      co_await seed.put(objects, key("doc:1"), bytes("old"));
      co_await seed.put(objects, key("doc:2"), bytes("two"));
      co_await seed.commit();

      auto snapshot = co_await driver.begin_read();
      auto update = co_await driver.begin_transaction();
      co_await update.put(objects, key("doc:1"), bytes("new"));
      co_await update.put(objects, key("doc:3"), bytes("three"));
      co_await update.commit();

      BOOST_CHECK_EQUAL(text(*(co_await snapshot.get(objects, key("doc:1")))), "old");

      auto page = co_await snapshot.scan_page(
         objects,
         forge::db::record_range{.begin = key("doc:"), .end = key("doc;"), .prefix = key("doc:")},
         forge::db::page_request{.limit = 1});
      BOOST_REQUIRE_EQUAL(page.entries.size(), 1U);
      BOOST_CHECK_EQUAL(text(page.entries.front().value), "old");
      BOOST_REQUIRE(page.next.has_value());

      auto second = co_await snapshot.scan_page(
         objects,
         forge::db::record_range{.begin = key("doc:"), .end = key("doc;"), .prefix = key("doc:")},
         forge::db::page_request{.after = page.next, .limit = 2});
      BOOST_REQUIRE_EQUAL(second.entries.size(), 1U);
      BOOST_CHECK_EQUAL(text(second.entries.front().value), "two");
      BOOST_CHECK(!second.next.has_value());
      co_return;
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_rocksdb_snapshot_scans_prefixless_half_open_ranges) {
   const auto root = make_test_root("forge_db_rocksdb_range");
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = forge::db::rocksdb::driver{config_for(root / "store")};
      const auto objects = forge::db::family{"objectdb"};

      auto seed = co_await driver.begin_transaction();
      for (const auto* value : {"a", "aa", "b", "y", "z"}) {
         co_await seed.put(objects, key(value), bytes(value));
      }
      co_await seed.commit();

      auto snapshot = co_await driver.begin_read();
      auto first = co_await snapshot.scan_page(
         objects,
         forge::db::record_range{.begin = key("a"), .end = key("z")},
         forge::db::page_request{.limit = 3});
      BOOST_REQUIRE_EQUAL(first.entries.size(), 3U);
      BOOST_CHECK_EQUAL(text(first.entries[0].value), "a");
      BOOST_CHECK_EQUAL(text(first.entries[1].value), "aa");
      BOOST_CHECK_EQUAL(text(first.entries[2].value), "b");
      BOOST_REQUIRE(first.next.has_value());

      auto second = co_await snapshot.scan_page(
         objects,
         forge::db::record_range{.begin = key("a"), .end = key("z")},
         forge::db::page_request{.after = first.next, .limit = 3});
      BOOST_REQUIRE_EQUAL(second.entries.size(), 1U);
      BOOST_CHECK_EQUAL(text(second.entries.front().value), "y");
      BOOST_CHECK(!second.next.has_value());
      co_return;
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_rocksdb_snapshot_scan_does_not_emit_cursor_at_range_end) {
   const auto root = make_test_root("forge_db_rocksdb_range_end_cursor");
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = forge::db::rocksdb::driver{config_for(root / "store")};
      const auto objects = forge::db::family{"objectdb"};

      auto seed = co_await driver.begin_transaction();
      for (const auto* value : {"a", "b", "c"}) {
         co_await seed.put(objects, key(value), bytes(value));
      }
      co_await seed.commit();

      auto snapshot = co_await driver.begin_read();
      auto page = co_await snapshot.scan_page(
         objects,
         forge::db::record_range{.begin = key("a"), .end = key("c")},
         forge::db::page_request{.limit = 2});
      BOOST_REQUIRE_EQUAL(page.entries.size(), 2U);
      BOOST_CHECK_EQUAL(text(page.entries[0].value), "a");
      BOOST_CHECK_EQUAL(text(page.entries[1].value), "b");
      BOOST_CHECK(!page.next.has_value());
      co_return;
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_rocksdb_snapshot_scan_preserves_empty_key_cursor) {
   const auto root = make_test_root("forge_db_rocksdb_empty_cursor");
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = forge::db::rocksdb::driver{config_for(root / "store")};
      const auto objects = forge::db::family{"objectdb"};

      auto seed = co_await driver.begin_transaction();
      co_await seed.put(objects, empty_key(), bytes("empty"));
      co_await seed.put(objects, key("next"), bytes("next"));
      co_await seed.commit();

      auto snapshot = co_await driver.begin_read();
      auto first = co_await snapshot.scan_page(
         objects,
         forge::db::record_range{.begin = empty_key(), .end = key("z")},
         forge::db::page_request{.limit = 1});
      BOOST_REQUIRE_EQUAL(first.entries.size(), 1U);
      BOOST_CHECK(first.entries.front().key.empty());
      BOOST_CHECK_EQUAL(text(first.entries.front().value), "empty");
      BOOST_REQUIRE(first.next.has_value());
      BOOST_CHECK(first.next->boundary.empty());

      auto second = co_await snapshot.scan_page(
         objects,
         forge::db::record_range{.begin = empty_key(), .end = key("z")},
         forge::db::page_request{.after = first.next, .limit = 1});
      BOOST_REQUIRE_EQUAL(second.entries.size(), 1U);
      BOOST_CHECK_EQUAL(text(second.entries.front().key.bytes()), "next");
      BOOST_CHECK_EQUAL(text(second.entries.front().value), "next");
      BOOST_CHECK(!second.next.has_value());
      co_return;
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(db_rocksdb_blob_enabled_family_roundtrips_large_values) {
   const auto root = make_test_root("forge_db_rocksdb_blob");
   auto runtime = forge::asio::runtime{};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto driver = forge::db::rocksdb::driver{config_for(root / "store")};
      const auto blobs = forge::db::family{"blobdb.data"};
      auto payload = std::vector<std::byte>(4096, std::byte{0x7b});

      auto tx = co_await driver.begin_transaction();
      co_await tx.put(blobs, key("large"), payload);
      co_await tx.commit();
      co_await driver.async_flush(true);

      auto snapshot = co_await driver.begin_read();
      const auto loaded = co_await snapshot.get(blobs, key("large"));
      BOOST_REQUIRE(loaded.has_value());
      BOOST_CHECK(*loaded == payload);
      co_return;
   }());

   std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_SUITE_END()
