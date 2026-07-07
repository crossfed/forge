module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <typeindex>
#include <utility>
#include <vector>

module forge.db.object.snapshot;

import forge.db.object.exceptions;

#include "details/snapshot_impl.hxx"

namespace forge::db::object {

snapshot::impl::impl(forge::db::snapshot active_value,
                     forge::db::family family_value,
                     snapshot::ensure_registered_fn ensure) noexcept
    : active{std::move(active_value)}, family{std::move(family_value)}, ensure_registered{std::move(ensure)} {}

snapshot::snapshot(forge::db::snapshot active, forge::db::family family, ensure_registered_fn ensure)
    : impl_{std::make_shared<impl>(std::move(active), std::move(family), std::move(ensure))} {}

void snapshot::ensure_registered_type(forge::ids::object_id type, std::type_index model) const {
   if (!impl_ || !impl_->ensure_registered) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db object snapshot is closed");
   }
   impl_->ensure_registered(type, model);
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>> snapshot::get_record(record_key key) const {
   if (!impl_ || !impl_->active.active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db object snapshot is closed");
   }
   co_return co_await impl_->active.get(impl_->family, std::move(key));
}

boost::asio::awaitable<record_page> snapshot::scan_records(record_range range, page_request request) const {
   if (!impl_ || !impl_->active.active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db object snapshot is closed");
   }
   co_return co_await impl_->active.scan_page(impl_->family, std::move(range), std::move(request));
}

} // namespace forge::db::object
