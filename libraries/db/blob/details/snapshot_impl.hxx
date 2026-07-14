#pragma once

namespace forge::db::blob {

struct snapshot::impl {
   impl(forge::db::core::snapshot active_value,
        forge::db::core::family data,
        forge::db::core::family refs) noexcept;

   forge::db::core::snapshot active;
   forge::db::core::family data_family;
   forge::db::core::family refs_family;
};

} // namespace forge::db::blob
