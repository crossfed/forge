#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/objectdb/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
import forge.blobdb.exceptions;
import forge.blobdb.store;
import forge.blobdb.types;
import forge.db.driver;
import forge.db.record;
import forge.ids.object_id;
import forge.objectdb.index;
import forge.objectdb.object;
import forge.objectdb.store;

namespace {

using record_map = std::map<forge::db::record_key, std::vector<std::byte>>;
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

bool starts_with(const forge::db::record_key& value, const forge::db::record_key& prefix) {
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

class memory_session final : public forge::db::session {
 public:
   memory_session(std::shared_ptr<memory_state> state, bool snapshot, bool writable)
       : state_{std::move(state)}, working_{state_->records}, snapshot_{snapshot}, writable_{writable} {}

   [[nodiscard]] forge::db::capabilities capabilities() const noexcept override {
      return forge::db::capabilities{.snapshot_reads = snapshot_, .writes = writable_};
   }

   boost::asio::awaitable<std::optional<std::vector<std::byte>>> get(forge::db::family family,
                                                                     forge::db::record_key key) override {
      const auto family_found = working_.find(family.name);
      if (family_found == working_.end()) {
         co_return std::nullopt;
      }
      const auto found = family_found->second.find(key);
      if (found == family_found->second.end()) {
         co_return std::nullopt;
      }
      co_return found->second;
   }

   boost::asio::awaitable<void> put(forge::db::family family,
                                    forge::db::record_key key,
                                    std::vector<std::byte> value) override {
      working_[family.name][std::move(key)] = std::move(value);
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::db::family family, forge::db::record_key key) override {
      working_[family.name].erase(key);
      co_return;
   }

   boost::asio::awaitable<forge::db::record_page> scan_page(forge::db::family family,
                                                            forge::db::record_range range,
                                                            forge::db::page_request request) override {
      forge::db::validate_page_request(request);
      auto result = forge::db::record_page{};
      const auto family_found = working_.find(family.name);
      if (family_found == working_.end()) {
         co_return result;
      }

      auto current = family_found->second.lower_bound(request.after ? request.after->boundary : range.begin);
      if (request.after && current != family_found->second.end() && current->first == request.after->boundary) {
         ++current;
      }

      auto last = std::optional<forge::db::record_key>{};
      while (current != family_found->second.end()) {
         if (!starts_with(current->first, range.prefix)) {
            break;
         }
         if (range.has_end && !(current->first.bytes() < range.end.bytes())) {
            break;
         }
         result.entries.push_back(forge::db::record_entry{.key = current->first, .value = current->second});
         last = current->first;
         ++current;
         if (result.entries.size() == request.limit) {
            break;
         }
      }
      if (last && current != family_found->second.end() && starts_with(current->first, range.prefix) &&
          (!range.has_end || current->first.bytes() < range.end.bytes())) {
         result.next = forge::db::cursor{.boundary = *last};
      }
      co_return result;
   }

   boost::asio::awaitable<void> commit() override {
      if (state_->fail_commit) {
         throw std::runtime_error{"blobdb test commit failure"};
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

class memory_driver final : public forge::db::driver {
 public:
   explicit memory_driver(std::shared_ptr<memory_state> state) : state_{std::move(state)} {}

   boost::asio::awaitable<void> async_flush(bool) override {
      co_return;
   }

 private:
   boost::asio::awaitable<std::unique_ptr<forge::db::session>> open_transaction() override {
      co_return std::make_unique<memory_session>(state_, false, true);
   }

   boost::asio::awaitable<std::unique_ptr<forge::db::session>> open_snapshot() override {
      co_return std::make_unique<memory_session>(state_, true, false);
   }

   std::shared_ptr<memory_state> state_;
};

class first_byte_hasher final : public forge::blobdb::hasher {
 public:
   forge::blobdb::digest hash(std::span<const std::byte> payload) const override {
      auto value = std::vector<std::byte>{std::byte{0x42}};
      value.push_back(payload.empty() ? std::byte{0} : payload.front());
      value.push_back(static_cast<std::byte>(payload.size() & 0xffU));
      return forge::blobdb::digest{std::move(value)};
   }
};

struct by_id;

struct document : forge::objectdb::object<document, 3, 9> {
   forge::blobdb::digest blob;
   std::string title;

   bool operator==(const document&) const = default;
};

BOOST_DESCRIBE_STRUCT(document, (forge::objectdb::object<document, 3, 9>), (blob, title))

using document_object =
   forge::objectdb::object_index<document, forge::objectdb::indexed_by<forge::objectdb::primary_unique<by_id>>>;

document make_document(std::uint64_t instance, forge::blobdb::digest digest) {
   auto value = document{};
   value.id = document::id_type{instance};
   value.blob = std::move(digest);
   value.title = "doc";
   return value;
}

} // namespace

FORGE_OBJECTDB_OBJECT(document_object)

BOOST_AUTO_TEST_SUITE(blobdb_test_suite)

BOOST_AUTO_TEST_CASE(blobdb_put_get_verify_and_digest_modes_work) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());
   auto settings = forge::blobdb::store::config{.digest_hasher = std::make_shared<first_byte_hasher>()};
   auto blobs = forge::blobdb::store{driver, settings};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto id = co_await blobs.put(bytes("alpha"));
      BOOST_CHECK(co_await blobs.has(id));
      BOOST_CHECK_EQUAL(text(co_await blobs.get(id)), "alpha");
      co_await blobs.verify(id);

      BOOST_CHECK_THROW(co_await blobs.put(forge::blobdb::digest{bytes("wrong")}, bytes("alpha")),
                        forge::blobdb::exceptions::digest_mismatch);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(blobdb_refs_and_collection_are_explicit_mechanisms) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());
   auto blobs = forge::blobdb::store{driver, forge::blobdb::store::config{.digest_hasher = std::make_shared<first_byte_hasher>()}};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto kept = co_await blobs.put(bytes("kept"));
      const auto free = co_await blobs.put(bytes("free"));
      co_await blobs.retain(kept, forge::blobdb::owner_ref{"doc:1"});

      BOOST_CHECK_EQUAL(co_await blobs.ref_count(kept), 1U);
      BOOST_CHECK_EQUAL(co_await blobs.ref_count(free), 0U);

      auto collected = co_await blobs.collect_unreferenced({.limit = 10});
      BOOST_CHECK_EQUAL(collected.removed, 1U);
      BOOST_CHECK(co_await blobs.has(kept));
      BOOST_CHECK(!(co_await blobs.has(free)));

      co_await blobs.release(kept, forge::blobdb::owner_ref{"doc:1"});
      BOOST_CHECK_EQUAL(co_await blobs.ref_count(kept), 0U);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(blobdb_join_shares_commit_boundary_with_objectdb_metadata) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());
   auto objects = forge::objectdb::store{driver};
   objects.register_object<document_object>();
   auto blobs = forge::blobdb::store{driver, forge::blobdb::store::config{.digest_hasher = std::make_shared<first_byte_hasher>()}};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto db_tx = co_await driver->begin_transaction();
      auto object_tx = objects.join(db_tx);
      auto blob_tx = blobs.join(db_tx);

      auto id = co_await blob_tx.put(bytes("payload"));
      co_await object_tx.insert(make_document(7, id));

      BOOST_CHECK(!(co_await blobs.has(id)));
      BOOST_CHECK(!(co_await objects.find(document::id_type{7})).has_value());

      co_await db_tx.commit();

      BOOST_CHECK(co_await blobs.has(id));
      const auto loaded = co_await objects.get(document::id_type{7});
      BOOST_CHECK(loaded.blob == id);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(blobdb_joined_transaction_does_not_own_commit) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());
   auto blobs = forge::blobdb::store{driver, forge::blobdb::store::config{.digest_hasher = std::make_shared<first_byte_hasher>()}};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto db_tx = co_await driver->begin_transaction();
      auto blob_tx = blobs.join(db_tx);
      BOOST_CHECK_THROW(co_await blob_tx.commit(), forge::blobdb::exceptions::unsupported_operation);
      co_await db_tx.rollback();
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(blobdb_direct_mutation_commit_failure_rolls_back) {
   auto runtime = forge::asio::runtime{};
   auto state = std::make_shared<memory_state>();
   state->fail_commit = true;
   auto driver = std::make_shared<memory_driver>(state);
   auto blobs = forge::blobdb::store{driver, forge::blobdb::store::config{.digest_hasher = std::make_shared<first_byte_hasher>()}};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      BOOST_CHECK_THROW(co_await blobs.put(bytes("alpha")), std::runtime_error);
      BOOST_CHECK_EQUAL(state->rollback_calls, 1U);

      state->fail_commit = false;
      const auto id = co_await blobs.put(bytes("beta"));
      BOOST_CHECK(co_await blobs.has(id));
      co_return;
   }());
}

BOOST_AUTO_TEST_SUITE_END()
