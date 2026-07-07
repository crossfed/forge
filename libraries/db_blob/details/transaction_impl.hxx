#pragma once

#include <optional>

namespace forge::db::blob {

struct transaction::impl {
   struct owned_tag {};
   struct borrowed_tag {};

   impl(owned_tag,
        forge::db::transaction active_value,
        forge::db::family data,
        forge::db::family refs) noexcept;

   impl(borrowed_tag,
        forge::db::transaction& active_value,
        forge::db::family data,
        forge::db::family refs) noexcept;

   std::optional<forge::db::transaction> owned;
   forge::db::transaction* active = nullptr;
   forge::db::family data_family;
   forge::db::family refs_family;
   bool owns_commit = false;

   [[nodiscard]] forge::db::transaction& transaction();
};

} // namespace forge::db::blob
