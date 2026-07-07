#pragma once

#include <memory>
#include <optional>

namespace forge::blobdb {

struct transaction::impl {
   struct owned_tag {};
   struct borrowed_tag {};

   impl(owned_tag,
        forge::db::transaction active_value,
        forge::db::family data,
        forge::db::family refs,
        std::shared_ptr<hasher> digest_hasher,
        bool verify_writes,
        bool verify_reads) noexcept;

   impl(borrowed_tag,
        forge::db::transaction& active_value,
        forge::db::family data,
        forge::db::family refs,
        std::shared_ptr<hasher> digest_hasher,
        bool verify_writes,
        bool verify_reads) noexcept;

   std::optional<forge::db::transaction> owned;
   forge::db::transaction* active = nullptr;
   forge::db::family data_family;
   forge::db::family refs_family;
   std::shared_ptr<hasher> digest_hasher;
   bool verify_on_write = true;
   bool verify_on_read = true;
   bool owns_commit = false;

   [[nodiscard]] forge::db::transaction& transaction();
};

} // namespace forge::blobdb
