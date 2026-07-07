#include <boost/asio/awaitable.hpp>
#include <boost/describe.hpp>
#include <boost/test/unit_test.hpp>
#include <forge/db/object/macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import forge.asio.blocking;
import forge.asio.runtime;
import forge.db.blob.exceptions;
import forge.db.blob.ref;
import forge.db.blob.store;
import forge.db.blob.types;
import forge.crypto.hex;
import forge.crypto.sha256;
import forge.db.core.driver;
import forge.db.core.record;
import forge.ids.object_id;
import forge.db.object.index;
import forge.db.object.object;
import forge.db.object.store;
import forge.raw.datastream;
import forge.raw.exceptions;
import forge.raw.raw;
import forge.variant.exceptions;
import forge.variant.value;

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
                                                                     forge::db::core::record_key key) override {
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

   boost::asio::awaitable<void> put(forge::db::core::family family,
                                    forge::db::core::record_key key,
                                    std::vector<std::byte> value) override {
      working_[family.name][std::move(key)] = std::move(value);
      co_return;
   }

   boost::asio::awaitable<void> erase(forge::db::core::family family, forge::db::core::record_key key) override {
      working_[family.name].erase(key);
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
         throw std::runtime_error{"db blob test commit failure"};
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

struct toy_digest {
   std::vector<std::byte> bytes;

   bool operator==(const toy_digest&) const = default;
   auto operator<=>(const toy_digest&) const = default;
};

struct strict_digest {
   std::vector<std::byte> bytes;

   strict_digest() = delete;
   explicit strict_digest(std::vector<std::byte> value) : bytes{std::move(value)} {}

   bool operator==(const strict_digest&) const = default;
   auto operator<=>(const strict_digest&) const = default;
};

struct by_id;

struct document : forge::db::object::object<document, 3, 9> {
   forge::db::blob::ref<forge::db::blob::digest> blob;
   std::string title;

   bool operator==(const document&) const = default;
};

BOOST_DESCRIBE_STRUCT(document, (forge::db::object::object<document, 3, 9>), (blob, title))

using document_object =
   forge::db::object::object_index<document, forge::db::object::indexed_by<forge::db::object::primary_unique<by_id>>>;

document make_document(std::uint64_t instance, forge::db::blob::ref<forge::db::blob::digest> ref) {
   auto value = document{};
   value.id = document::id_type{instance};
   value.blob = std::move(ref);
   value.title = "doc";
   return value;
}

} // namespace

namespace forge::db::blob {

template <>
struct hash<toy_digest> {
   [[nodiscard]] toy_digest operator()(std::span<const std::byte> payload) const {
      return toy_digest{std::vector<std::byte>{payload.begin(), payload.end()}};
   }
};

template <>
struct digest_traits<toy_digest> {
   static constexpr auto algorithm = std::string_view{"toy"};

   [[nodiscard]] static std::vector<std::byte> to_bytes(const toy_digest& value) {
      return value.bytes;
   }

   [[nodiscard]] static toy_digest from_bytes(std::span<const std::byte> value) {
      return toy_digest{std::vector<std::byte>{value.begin(), value.end()}};
   }

   [[nodiscard]] static std::string text(const toy_digest& value) {
      return forge::crypto::to_hex(
         reinterpret_cast<const std::uint8_t*>(value.bytes.data()),
         static_cast<std::uint32_t>(value.bytes.size()));
   }

   [[nodiscard]] static toy_digest from_text(std::string_view value) {
      auto decoded = std::vector<std::uint8_t>(value.size() / 2U);
      forge::crypto::from_hex(std::string{value}, decoded.data(), decoded.size());
      auto bytes = std::vector<std::byte>{};
      bytes.reserve(decoded.size());
      for (const auto byte : decoded) {
         bytes.push_back(static_cast<std::byte>(byte));
      }
      return toy_digest{std::move(bytes)};
   }
};

template <>
struct hash<strict_digest> {
   [[nodiscard]] strict_digest operator()(std::span<const std::byte> payload) const {
      return strict_digest{std::vector<std::byte>{payload.begin(), payload.end()}};
   }
};

template <>
struct digest_traits<strict_digest> {
   static constexpr auto algorithm = std::string_view{"strict"};

   [[nodiscard]] static std::vector<std::byte> to_bytes(const strict_digest& value) {
      return value.bytes;
   }

   [[nodiscard]] static strict_digest from_bytes(std::span<const std::byte> value) {
      return strict_digest{std::vector<std::byte>{value.begin(), value.end()}};
   }

   [[nodiscard]] static std::string text(const strict_digest& value) {
      return forge::crypto::to_hex(
         reinterpret_cast<const std::uint8_t*>(value.bytes.data()),
         static_cast<std::uint32_t>(value.bytes.size()));
   }

   [[nodiscard]] static strict_digest from_text(std::string_view value) {
      auto decoded = std::vector<std::uint8_t>(value.size() / 2U);
      forge::crypto::from_hex(std::string{value}, decoded.data(), decoded.size());
      auto bytes = std::vector<std::byte>{};
      bytes.reserve(decoded.size());
      for (const auto byte : decoded) {
         bytes.push_back(static_cast<std::byte>(byte));
      }
      return strict_digest{std::move(bytes)};
   }
};

} // namespace forge::db::blob

FORGE_DB_OBJECT(document_object)

BOOST_AUTO_TEST_SUITE(db_blob_test_suite)

BOOST_AUTO_TEST_CASE(db_blob_ref_defaults_to_sha256_and_variant_uses_text_form) {
   using ref_type = forge::db::blob::ref<>;

   auto value = ref_type{
      .digest = forge::db::blob::hash<forge::db::blob::digest>{}(bytes("db-blob-ref")),
      .size = 12345,
   };

   auto encoded = forge::variant{};
   forge::to_variant(value, encoded);

   const auto expected = value.digest.str() + ":12345";
   BOOST_CHECK(encoded.is_string());
   BOOST_CHECK_EQUAL(encoded.get_string(), expected);

   auto decoded = ref_type{};
   forge::from_variant(encoded, decoded);
   BOOST_CHECK(decoded.digest == value.digest);
   BOOST_CHECK_EQUAL(decoded.size, value.size);

   auto defaults = ref_type{};
   BOOST_CHECK(defaults.digest.empty());
   BOOST_CHECK_EQUAL(defaults.size, 0U);
}

BOOST_AUTO_TEST_CASE(db_blob_ref_variant_rejects_invalid_text_form) {
   auto decoded = forge::db::blob::ref<>{};

   BOOST_CHECK_THROW(forge::from_variant(forge::variant{"missing-colon"}, decoded), forge::variant_exceptions::decode_error);
   BOOST_CHECK_THROW(forge::from_variant(forge::variant{":1"}, decoded), forge::variant_exceptions::decode_error);
   BOOST_CHECK_THROW(forge::from_variant(forge::variant{std::string(64, '0') + ":"}, decoded),
                     forge::variant_exceptions::decode_error);
   BOOST_CHECK_THROW(forge::from_variant(forge::variant{std::string(64, 'z') + ":1"}, decoded),
                     forge::variant_exceptions::decode_error);
   BOOST_CHECK_THROW(forge::from_variant(forge::variant{std::string(64, '0') + ":12x"}, decoded),
                     forge::variant_exceptions::decode_error);
   BOOST_CHECK_THROW(forge::from_variant(forge::variant{std::string(64, '0') + ":18446744073709551616"}, decoded),
                     forge::variant_exceptions::decode_error);
}

BOOST_AUTO_TEST_CASE(db_blob_ref_raw_roundtrip_is_compact_binary) {
   auto value = forge::db::blob::ref<>{
      .digest = forge::db::blob::hash<forge::db::blob::digest>{}(bytes("db-blob-raw-ref")),
      .size = 777,
   };

   const auto packed = forge::raw::pack(value);
   BOOST_CHECK_EQUAL(packed.size(), value.digest.data_size() + sizeof(std::uint64_t));

   const auto decoded = forge::raw::unpack<forge::db::blob::ref<>>(packed);
   BOOST_CHECK(decoded.digest == value.digest);
   BOOST_CHECK_EQUAL(decoded.size, value.size);
}

BOOST_AUTO_TEST_CASE(db_blob_ref_raw_uses_digest_traits_for_custom_digest) {
   auto value = forge::db::blob::ref<toy_digest>{
      .digest = toy_digest{bytes("\xde\xad")},
      .size = 777,
   };

   const auto packed = forge::raw::pack(value);
   BOOST_CHECK_EQUAL(packed.size(), sizeof(std::uint32_t) + value.digest.bytes.size() + sizeof(std::uint64_t));

   const auto decoded = forge::raw::unpack<forge::db::blob::ref<toy_digest>>(packed);
   BOOST_CHECK(decoded.digest == value.digest);
   BOOST_CHECK_EQUAL(decoded.size, value.size);
}

BOOST_AUTO_TEST_CASE(db_blob_ref_raw_roundtrip_supports_streambuf_datastream) {
   auto value = forge::db::blob::ref<>{
      .digest = forge::db::blob::hash<forge::db::blob::digest>{}(bytes("db-blob-streambuf-ref")),
      .size = 888,
   };
   const auto packed = forge::raw::pack(value);
   const auto packed_text = std::string{reinterpret_cast<const char*>(packed.data()), packed.size()};

   auto stream = forge::datastream<std::stringbuf>{packed_text, std::ios_base::in};
   auto decoded = forge::db::blob::ref<>{};
   stream >> decoded;

   BOOST_CHECK(decoded.digest == value.digest);
   BOOST_CHECK_EQUAL(decoded.size, value.size);
}

BOOST_AUTO_TEST_CASE(db_blob_ref_raw_rejects_truncated_streambuf_payload) {
   auto value = forge::db::blob::ref<>{
      .digest = forge::db::blob::hash<forge::db::blob::digest>{}(bytes("db-blob-truncated-ref")),
      .size = 999,
   };
   auto packed = forge::raw::pack(value);
   packed.pop_back();
   const auto packed_text = std::string{reinterpret_cast<const char*>(packed.data()), packed.size()};

   auto stream = forge::datastream<std::stringbuf>{packed_text, std::ios_base::in};
   auto decoded = forge::db::blob::ref<>{};
   BOOST_CHECK_THROW(stream >> decoded, forge::raw::exceptions::codec_error);
}

BOOST_AUTO_TEST_CASE(db_blob_ref_supports_custom_digest_text_roundtrip) {
   auto value = forge::db::blob::ref<toy_digest>{
      .digest = toy_digest{bytes("\xde\xad")},
      .size = 7,
   };

   auto encoded = forge::variant{};
   forge::to_variant(value, encoded);
   BOOST_CHECK_EQUAL(encoded.get_string(), "dead:7");

   auto decoded = forge::db::blob::ref<toy_digest>{};
   forge::from_variant(encoded, decoded);
   BOOST_CHECK(decoded.digest == value.digest);
   BOOST_CHECK_EQUAL(decoded.size, value.size);
}

BOOST_AUTO_TEST_CASE(db_blob_put_returns_default_ref_and_default_operations_use_refs) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());
   auto blobs = forge::db::blob::store{driver};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto ref = co_await blobs.put(bytes("alpha"));
      BOOST_CHECK(ref.digest == forge::db::blob::hash<forge::db::blob::digest>{}(bytes("alpha")));
      BOOST_CHECK_EQUAL(ref.size, 5U);
      BOOST_CHECK(co_await blobs.has(ref));
      BOOST_CHECK_EQUAL(text(co_await blobs.get(ref)), "alpha");
      co_await blobs.verify(ref);

      auto wrong_size = ref;
      wrong_size.size = ref.size + 1U;
      BOOST_CHECK(!(co_await blobs.has(wrong_size)));
      BOOST_CHECK_THROW(co_await blobs.stat_blob(wrong_size), forge::db::blob::exceptions::digest_mismatch);
      BOOST_CHECK_THROW(co_await blobs.get(wrong_size), forge::db::blob::exceptions::digest_mismatch);
      BOOST_CHECK_THROW(co_await blobs.verify(wrong_size), forge::db::blob::exceptions::digest_mismatch);

      auto wrong = ref;
      wrong.digest = forge::db::blob::hash<forge::db::blob::digest>{}(bytes("wrong"));
      BOOST_CHECK_THROW(co_await blobs.put(wrong, bytes("alpha")),
                        forge::db::blob::exceptions::digest_mismatch);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_blob_put_as_supports_custom_digest_without_templated_store) {
   static_assert(forge::db::blob::digest_algorithm<toy_digest>);
   static_assert(forge::db::blob::digest_algorithm<strict_digest>);
   static_assert(!std::default_initializable<strict_digest>);

   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());
   auto blobs = forge::db::blob::store{driver};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto ref = co_await blobs.put_as<toy_digest>(bytes("alpha"));
      BOOST_CHECK(ref.digest == forge::db::blob::hash<toy_digest>{}(bytes("alpha")));
      BOOST_CHECK_EQUAL(ref.size, 5U);
      BOOST_CHECK(co_await blobs.has(ref));
      BOOST_CHECK_EQUAL(text(co_await blobs.get(ref)), "alpha");

      auto strict_ref = co_await blobs.put_as<strict_digest>(bytes("bravo"));
      BOOST_CHECK(strict_ref.digest == forge::db::blob::hash<strict_digest>{}(bytes("bravo")));
      BOOST_CHECK_EQUAL(strict_ref.size, 5U);
      BOOST_CHECK(co_await blobs.has(strict_ref));
      BOOST_CHECK_EQUAL(text(co_await blobs.get(strict_ref)), "bravo");
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_blob_refs_and_collection_are_explicit_mechanisms) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());
   auto blobs = forge::db::blob::store{driver};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto kept = co_await blobs.put(bytes("kept"));
      const auto free = co_await blobs.put(bytes("free"));
      co_await blobs.retain(kept, forge::db::blob::owner_ref{"doc:1"});

      BOOST_CHECK_EQUAL(co_await blobs.ref_count(kept), 1U);
      BOOST_CHECK_EQUAL(co_await blobs.ref_count(free), 0U);

      auto collected = co_await blobs.collect_unreferenced({.limit = 10});
      BOOST_CHECK_EQUAL(collected.removed, 1U);
      BOOST_CHECK(co_await blobs.has(kept));
      BOOST_CHECK(!(co_await blobs.has(free)));

      co_await blobs.release(kept, forge::db::blob::owner_ref{"doc:1"});
      BOOST_CHECK_EQUAL(co_await blobs.ref_count(kept), 0U);
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_blob_ref_keys_do_not_alias_variable_length_digests) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());
   auto blobs = forge::db::blob::store{driver};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      const auto short_payload = std::vector<std::byte>{std::byte{0x01}};
      const auto long_payload = std::vector<std::byte>{std::byte{0x01}, std::byte{0x00}, std::byte{0x02}};
      const auto short_ref = forge::db::blob::ref<toy_digest>{.digest = toy_digest{short_payload}, .size = short_payload.size()};
      const auto long_ref = forge::db::blob::ref<toy_digest>{.digest = toy_digest{long_payload}, .size = long_payload.size()};

      co_await blobs.put(short_ref, short_payload);
      co_await blobs.put(long_ref, long_payload);
      co_await blobs.retain(long_ref, forge::db::blob::owner_ref{"doc:long"});

      BOOST_CHECK_EQUAL(co_await blobs.ref_count(short_ref), 0U);
      BOOST_CHECK_EQUAL(co_await blobs.ref_count(long_ref), 1U);

      auto collected = co_await blobs.collect_unreferenced({.limit = 10});
      BOOST_CHECK_EQUAL(collected.removed, 1U);
      BOOST_CHECK(!(co_await blobs.has(short_ref)));
      BOOST_CHECK(co_await blobs.has(long_ref));
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_blob_join_shares_commit_boundary_with_db_object_metadata) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());
   auto objects = forge::db::object::store{driver};
   objects.register_object<document_object>();
   auto blobs = forge::db::blob::store{driver};

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

BOOST_AUTO_TEST_CASE(db_blob_joined_transaction_does_not_own_commit) {
   auto runtime = forge::asio::runtime{};
   auto driver = std::make_shared<memory_driver>(std::make_shared<memory_state>());
   auto blobs = forge::db::blob::store{driver};

   forge::asio::blocking::run(runtime, [&]() -> boost::asio::awaitable<void> {
      auto db_tx = co_await driver->begin_transaction();
      auto blob_tx = blobs.join(db_tx);
      BOOST_CHECK_THROW(co_await blob_tx.commit(), forge::db::blob::exceptions::unsupported_operation);
      co_await db_tx.rollback();
      co_return;
   }());
}

BOOST_AUTO_TEST_CASE(db_blob_direct_mutation_commit_failure_rolls_back) {
   auto runtime = forge::asio::runtime{};
   auto state = std::make_shared<memory_state>();
   state->fail_commit = true;
   auto driver = std::make_shared<memory_driver>(state);
   auto blobs = forge::db::blob::store{driver};

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
