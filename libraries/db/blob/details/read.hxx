#pragma once

#include "key_codec.hxx"

namespace forge::db::blob::detail {

inline void require_encoded_ref(const std::string& algorithm,
                                const std::vector<std::byte>& digest) {
   if (algorithm.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                            "blob digest algorithm must not be empty");
   }
   if (digest.empty()) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_config,
                            "blob digest must not be empty");
   }
}

template <typename Access>
boost::asio::awaitable<std::vector<std::byte>> read_payload(
   Access& active,
   const forge::db::core::family& data_family,
   const std::string& algorithm,
   const std::vector<std::byte>& digest,
   std::uint64_t size) {
   require_encoded_ref(algorithm, digest);
   auto bytes = co_await active.get(data_family, data_key(algorithm, digest));
   if (!bytes.has_value()) {
      FORGE_THROW_EXCEPTION(exceptions::not_found, "blob was not found");
   }
   if (bytes->size() != size) {
      FORGE_THROW_EXCEPTION(exceptions::digest_mismatch,
                            "blob size does not match reference");
   }
   co_return std::move(*bytes);
}

template <typename Access>
boost::asio::awaitable<bool> has_payload(
   Access& active,
   const forge::db::core::family& data_family,
   const std::string& algorithm,
   const std::vector<std::byte>& digest,
   std::uint64_t size) {
   require_encoded_ref(algorithm, digest);
   auto bytes = co_await active.get(data_family, data_key(algorithm, digest));
   co_return bytes.has_value() && bytes->size() == size;
}

template <typename Access>
boost::asio::awaitable<std::uint64_t> count_refs(
   Access& active,
   const forge::db::core::family& refs_family,
   const std::string& algorithm,
   const std::vector<std::byte>& digest) {
   require_encoded_ref(algorithm, digest);
   auto count = std::uint64_t{};
   auto request = forge::db::core::page_request{.limit = 100};
   const auto prefix = ref_prefix(algorithm, digest);
   while (true) {
      auto page = co_await active.scan_page(
         refs_family,
         forge::db::core::record_range{
            .begin = prefix,
            .prefix = prefix,
            .has_end = false,
         },
         request);
      count += page.entries.size();
      if (!page.next.has_value()) {
         break;
      }
      request.after = std::move(page.next);
   }
   co_return count;
}

template <typename Access>
boost::asio::awaitable<stat> read_stat(
   Access& active,
   const forge::db::core::family& data_family,
   const forge::db::core::family& refs_family,
   const std::string& algorithm,
   const std::vector<std::byte>& digest,
   std::uint64_t size) {
   auto bytes = co_await read_payload(
      active, data_family, algorithm, digest, size);
   co_return stat{
      .size = static_cast<std::uint64_t>(bytes.size()),
      .refs = co_await count_refs(active, refs_family, algorithm, digest),
   };
}

} // namespace forge::db::blob::detail
