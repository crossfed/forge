#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace forge::blobdb::detail {

struct encoded_ref {
   std::string algorithm;
   std::vector<std::byte> digest;
};

inline void append_u32_be(std::vector<std::byte>& key, std::uint32_t value) {
   key.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
   key.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
   key.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
   key.push_back(static_cast<std::byte>(value & 0xffU));
}

inline bool read_u32_be(const std::vector<std::byte>& bytes, std::size_t& offset, std::uint32_t& value) {
   if (offset > bytes.size() || bytes.size() - offset < 4U) {
      return false;
   }
   value = (std::to_integer<std::uint32_t>(bytes[offset]) << 24U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           std::to_integer<std::uint32_t>(bytes[offset + 3U]);
   offset += 4U;
   return true;
}

inline void append_bytes(std::vector<std::byte>& key, std::span<const std::byte> value) {
   append_u32_be(key, static_cast<std::uint32_t>(value.size()));
   key.insert(key.end(), value.begin(), value.end());
}

inline void append_algorithm(std::vector<std::byte>& key, std::string_view value) {
   const auto* begin = reinterpret_cast<const std::byte*>(value.data());
   append_bytes(key, std::span<const std::byte>{begin, begin + value.size()});
}

inline void append_blob_ref(std::vector<std::byte>& key,
                            std::string_view algorithm,
                            std::span<const std::byte> digest) {
   append_algorithm(key, algorithm);
   append_bytes(key, digest);
}

inline forge::db::record_key data_key(std::string_view algorithm, std::span<const std::byte> digest) {
   auto key = std::vector<std::byte>{};
   key.reserve(algorithm.size() + digest.size() + 9U);
   key.push_back(static_cast<std::byte>(0x10U));
   append_blob_ref(key, algorithm, digest);
   return forge::db::record_key{std::move(key)};
}

inline forge::db::record_key ref_key(std::string_view algorithm, std::span<const std::byte> digest, const owner_ref& owner) {
   auto key = std::vector<std::byte>{};
   key.reserve(algorithm.size() + digest.size() + owner.bytes.size() + 9U);
   key.push_back(static_cast<std::byte>(0x20U));
   append_blob_ref(key, algorithm, digest);
   key.insert(key.end(), owner.bytes.begin(), owner.bytes.end());
   return forge::db::record_key{std::move(key)};
}

inline forge::db::record_key ref_prefix(std::string_view algorithm, std::span<const std::byte> digest) {
   auto key = std::vector<std::byte>{};
   key.reserve(algorithm.size() + digest.size() + 9U);
   key.push_back(static_cast<std::byte>(0x20U));
   append_blob_ref(key, algorithm, digest);
   return forge::db::record_key{std::move(key)};
}

inline forge::db::record_key data_prefix() {
   return forge::db::record_key{std::vector<std::byte>{static_cast<std::byte>(0x10U)}};
}

inline std::optional<encoded_ref> ref_from_data_key(const forge::db::record_key& key) {
   const auto& bytes = key.bytes();
   if (bytes.size() < 9U || bytes.front() != static_cast<std::byte>(0x10U)) {
      return std::nullopt;
   }

   auto offset = std::size_t{1U};
   auto algorithm_size = std::uint32_t{};
   if (!read_u32_be(bytes, offset, algorithm_size) || bytes.size() - offset < algorithm_size) {
      return std::nullopt;
   }
   auto algorithm = std::string{
      reinterpret_cast<const char*>(bytes.data() + offset),
      reinterpret_cast<const char*>(bytes.data() + offset + algorithm_size)};
   offset += algorithm_size;

   auto digest_size = std::uint32_t{};
   if (!read_u32_be(bytes, offset, digest_size) || bytes.size() - offset != digest_size) {
      return std::nullopt;
   }
   auto digest = std::vector<std::byte>{bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end()};
   return encoded_ref{.algorithm = std::move(algorithm), .digest = std::move(digest)};
}

} // namespace forge::blobdb::detail
