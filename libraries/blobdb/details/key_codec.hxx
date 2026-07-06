#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace forge::blobdb::detail {

inline void append_u32_be(std::vector<std::byte>& key, std::uint32_t value) {
   key.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
   key.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
   key.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
   key.push_back(static_cast<std::byte>(value & 0xffU));
}

inline void append_digest(std::vector<std::byte>& key, const digest& id) {
   append_u32_be(key, static_cast<std::uint32_t>(id.bytes.size()));
   key.insert(key.end(), id.bytes.begin(), id.bytes.end());
}

inline forge::db::record_key data_key(const digest& id) {
   auto key = std::vector<std::byte>{};
   key.reserve(id.bytes.size() + 1U);
   key.push_back(static_cast<std::byte>(0x10U));
   key.insert(key.end(), id.bytes.begin(), id.bytes.end());
   return forge::db::record_key{std::move(key)};
}

inline forge::db::record_key ref_key(const digest& id, const owner_ref& owner) {
   auto key = std::vector<std::byte>{};
   key.reserve(id.bytes.size() + owner.bytes.size() + 5U);
   key.push_back(static_cast<std::byte>(0x20U));
   append_digest(key, id);
   key.insert(key.end(), owner.bytes.begin(), owner.bytes.end());
   return forge::db::record_key{std::move(key)};
}

inline forge::db::record_key ref_prefix(const digest& id) {
   auto key = std::vector<std::byte>{};
   key.reserve(id.bytes.size() + 5U);
   key.push_back(static_cast<std::byte>(0x20U));
   append_digest(key, id);
   return forge::db::record_key{std::move(key)};
}

inline forge::db::record_key data_prefix() {
   return forge::db::record_key{std::vector<std::byte>{static_cast<std::byte>(0x10U)}};
}

} // namespace forge::blobdb::detail
