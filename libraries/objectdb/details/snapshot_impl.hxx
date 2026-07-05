#pragma once

namespace forge::objectdb {

struct snapshot::impl {
   impl(forge::db::snapshot active_value, forge::db::family family_value, snapshot::ensure_registered_fn ensure) noexcept;

   forge::db::snapshot active;
   forge::db::family family;
   snapshot::ensure_registered_fn ensure_registered;
};

} // namespace forge::objectdb
