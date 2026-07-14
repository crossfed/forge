module;

#include <forge/exceptions/macros.hpp>

#include "snapshot_access.hxx"

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

export module forge.db.blob.snapshot;

import forge.db.blob.exceptions;
import forge.db.blob.ref;
import forge.db.blob.types;
import forge.db.core.driver;
import forge.db.core.record;

namespace forge::db::blob::detail {

template <digest_algorithm Digest>
[[nodiscard]] std::string snapshot_algorithm_id() {
   return std::string{digest_traits<Digest>::algorithm};
}

template <digest_algorithm Digest>
[[nodiscard]] std::vector<std::byte> snapshot_digest_bytes(
   const ref<Digest>& value) {
   return digest_traits<Digest>::to_bytes(value.digest);
}

template <digest_algorithm Digest>
void require_snapshot_payload_matches(const ref<Digest>& value,
                                      std::span<const std::byte> bytes) {
   if (value.size != bytes.size() || hash<Digest>{}(bytes) != value.digest) {
      FORGE_THROW_EXCEPTION(exceptions::digest_mismatch,
                            "blob digest does not match payload");
   }
}

} // namespace forge::db::blob::detail

export namespace forge::db::blob {

class snapshot {
 public:
   snapshot() = default;

   [[nodiscard]] bool active() const noexcept;

   template <digest_algorithm Digest>
   boost::asio::awaitable<std::vector<std::byte>> get(ref<Digest> value) {
      auto bytes = co_await get_encoded(
         detail::snapshot_algorithm_id<Digest>(),
         detail::snapshot_digest_bytes(value),
         value.size);
      detail::require_snapshot_payload_matches(value, bytes);
      co_return bytes;
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<bool> has(ref<Digest> value) {
      co_return co_await has_encoded(
         detail::snapshot_algorithm_id<Digest>(),
         detail::snapshot_digest_bytes(value),
         value.size);
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<stat> stat_blob(ref<Digest> value) {
      co_return co_await stat_blob_encoded(
         detail::snapshot_algorithm_id<Digest>(),
         detail::snapshot_digest_bytes(value),
         value.size);
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<void> verify(ref<Digest> value) {
      static_cast<void>(co_await get(std::move(value)));
   }

   template <digest_algorithm Digest>
   boost::asio::awaitable<std::uint64_t> ref_count(ref<Digest> value) {
      co_return co_await ref_count_encoded(
         detail::snapshot_algorithm_id<Digest>(),
         detail::snapshot_digest_bytes(value));
   }

 private:
   snapshot(forge::db::core::snapshot active,
            forge::db::core::family data_family,
            forge::db::core::family refs_family);

   boost::asio::awaitable<std::vector<std::byte>> get_encoded(
      std::string algorithm,
      std::vector<std::byte> digest,
      std::uint64_t size);
   boost::asio::awaitable<bool> has_encoded(
      std::string algorithm,
      std::vector<std::byte> digest,
      std::uint64_t size);
   boost::asio::awaitable<stat> stat_blob_encoded(
      std::string algorithm,
      std::vector<std::byte> digest,
      std::uint64_t size);
   boost::asio::awaitable<std::uint64_t> ref_count_encoded(
      std::string algorithm,
      std::vector<std::byte> digest);

   struct impl;
   std::shared_ptr<impl> impl_;

   friend class detail::snapshot_access;
};

} // namespace forge::db::blob
