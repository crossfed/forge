#pragma once

namespace forge::db::object {

struct snapshot::impl {
   impl(forge::db::core::snapshot active_value, forge::db::core::family family_value, snapshot::ensure_registered_fn ensure) noexcept;

   forge::db::core::snapshot active;
   forge::db::core::family family;
   snapshot::ensure_registered_fn ensure_registered;
};

} // namespace forge::db::object
