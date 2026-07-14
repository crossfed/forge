#pragma once

#include <optional>

namespace forge::db::blob {

namespace detail {
class transaction_participant_impl;
}

struct transaction::impl {
   struct owned_tag {};
   struct borrowed_tag {};

   impl(owned_tag,
        forge::db::core::transaction active_value,
        forge::db::core::family data,
        forge::db::core::family refs);

   impl(borrowed_tag,
        forge::db::core::transaction& active_value,
        forge::db::core::family data,
        forge::db::core::family refs);

   std::optional<forge::db::core::transaction> owned;
   forge::db::core::transaction* active = nullptr;
   forge::db::core::family data_family;
   forge::db::core::family refs_family;
   std::shared_ptr<detail::transaction_participant_impl> participant;
   bool owns_commit = false;

   [[nodiscard]] forge::db::core::transaction& transaction();
};

} // namespace forge::db::blob
