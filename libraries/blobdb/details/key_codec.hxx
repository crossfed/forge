#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace forge::blobdb::detail {

inline forge::db::record_key data_key(const digest& id) {
   auto key = std::vector<std::byte>{};
   key.reserve(id.bytes.size() + 1U);
   key.push_back(static_cast<std::byte>(0x10U));
   key.insert(key.end(), id.bytes.begin(), id.bytes.end());
   return forge::db::record_key{std::move(key)};
}

inline forge::db::record_key ref_key(const digest& id, const owner_ref& owner) {
   auto key = std::vector<std::byte>{};
   key.reserve(id.bytes.size() + owner.bytes.size() + 2U);
   key.push_back(static_cast<std::byte>(0x20U));
   key.insert(key.end(), id.bytes.begin(), id.bytes.end());
   key.push_back(static_cast<std::byte>(0U));
   key.insert(key.end(), owner.bytes.begin(), owner.bytes.end());
   return forge::db::record_key{std::move(key)};
}

inline forge::db::record_key ref_prefix(const digest& id) {
   auto key = std::vector<std::byte>{};
   key.reserve(id.bytes.size() + 2U);
   key.push_back(static_cast<std::byte>(0x20U));
   key.insert(key.end(), id.bytes.begin(), id.bytes.end());
   key.push_back(static_cast<std::byte>(0U));
   return forge::db::record_key{std::move(key)};
}

inline forge::db::record_key data_prefix() {
   return forge::db::record_key{std::vector<std::byte>{static_cast<std::byte>(0x10U)}};
}

} // namespace forge::blobdb::detail
