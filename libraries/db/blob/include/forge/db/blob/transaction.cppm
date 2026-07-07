module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module forge.db.blob.transaction;

import forge.db.blob.exceptions;
import forge.db.blob.ref;
import forge.db.blob.types;
import forge.db.core.driver;
import forge.db.core.record;

namespace forge::db::blob::detail {

template <digest_algorithm Digest>
[[nodiscard]] std::string algorithm_id() {
   return std::string{digest_traits<Digest>::algorithm};
}

template <digest_algorithm Digest>
[[nodiscard]] std::vector<std::byte> digest_bytes(const ref<Digest>& value) {
   return digest_traits<Digest>::to_bytes(value.digest);
}

template <digest_algorithm Digest>
void require_payload_matches(const ref<Digest>& value, std::span<const std::byte> bytes) {
   if (value.size != bytes.size() || hash<Digest>{}(bytes) != value.digest) {
      FORGE_THROW_EXCEPTION(exceptions::digest_mismatch, "blob digest does not match payload");
   }
}

} // namespace forge::db::blob::detail

export namespace forge::db::blob {

class transaction {
 public:
   transaction() = default;
   transaction(forge::db::core::transaction&& active,
               forge::db::core::family data_family,
               forge::db::core::family refs_family);
   transaction(forge::db::core::transaction& active,
               forge::db::core::family data_family,
               forge::db::core::family refs_family);

   [[nodiscard]] forge::db::core::transaction& db_transaction() const;

   boost::asio::awaitable<ref<digest>> put(std::vector<std::byte> payload);

   template <digest_algorithm Digest>
   boost::asio::awaitable<ref<Digest>> put_as(std::vector<std::byte> payload) {
      auto value = ref<Digest>{
         .digest = hash<Digest>{}(payload),
         .size = static_cast<std::uint64_t>(payload.size()),
      };
      co_await put(value, std::move(payload));
      co_return value;
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<void> put(ref<Digest> value, std::vector<std::byte> payload) {
      detail::require_payload_matches(value, payload);
      co_await put_encoded(
         detail::algorithm_id<Digest>(),
         detail::digest_bytes(value),
         value.size,
         std::move(payload));
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<std::vector<std::byte>> get(ref<Digest> value) {
      auto bytes = co_await get_encoded(detail::algorithm_id<Digest>(), detail::digest_bytes(value), value.size);
      detail::require_payload_matches(value, bytes);
      co_return bytes;
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<bool> has(ref<Digest> value) {
      co_return co_await has_encoded(detail::algorithm_id<Digest>(), detail::digest_bytes(value), value.size);
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<stat> stat_blob(ref<Digest> value) {
      co_return co_await stat_blob_encoded(detail::algorithm_id<Digest>(), detail::digest_bytes(value), value.size);
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<void> erase(ref<Digest> value) {
      co_await erase_encoded(detail::algorithm_id<Digest>(), detail::digest_bytes(value));
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<void> verify(ref<Digest> value) {
      static_cast<void>(co_await get(value));
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<void> retain(ref<Digest> value, owner_ref owner) {
      co_await retain_encoded(detail::algorithm_id<Digest>(), detail::digest_bytes(value), std::move(owner));
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<void> release(ref<Digest> value, owner_ref owner) {
      co_await release_encoded(detail::algorithm_id<Digest>(), detail::digest_bytes(value), std::move(owner));
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<std::uint64_t> ref_count(ref<Digest> value) {
      co_return co_await ref_count_encoded(detail::algorithm_id<Digest>(), detail::digest_bytes(value));
   }

   boost::asio::awaitable<collect_result> collect_unreferenced(collect_options options = {});

   boost::asio::awaitable<void> commit();
   boost::asio::awaitable<void> rollback();

 private:
   boost::asio::awaitable<void> put_encoded(std::string algorithm,
                                            std::vector<std::byte> digest,
                                            std::uint64_t size,
                                            std::vector<std::byte> payload);
   boost::asio::awaitable<std::vector<std::byte>> get_encoded(std::string algorithm,
                                                              std::vector<std::byte> digest,
                                                              std::uint64_t size);
   boost::asio::awaitable<bool> has_encoded(std::string algorithm, std::vector<std::byte> digest, std::uint64_t size);
   boost::asio::awaitable<stat> stat_blob_encoded(std::string algorithm,
                                                  std::vector<std::byte> digest,
                                                  std::uint64_t size);
   boost::asio::awaitable<void> erase_encoded(std::string algorithm, std::vector<std::byte> digest);
   boost::asio::awaitable<void> retain_encoded(std::string algorithm, std::vector<std::byte> digest, owner_ref owner);
   boost::asio::awaitable<void> release_encoded(std::string algorithm, std::vector<std::byte> digest, owner_ref owner);
   boost::asio::awaitable<std::uint64_t> ref_count_encoded(std::string algorithm, std::vector<std::byte> digest);

   struct impl;
   std::shared_ptr<impl> impl_;
};

} // namespace forge::db::blob
