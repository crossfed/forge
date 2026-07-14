module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

module forge.db.core.driver;

import forge.db.core.exceptions;

namespace forge::db::core {

snapshot::snapshot(std::unique_ptr<session> active)
    : snapshot{std::move(active), {}} {}

snapshot::snapshot(std::unique_ptr<session> active, std::shared_ptr<const void> origin)
    : active_{std::move(active)}, origin_{std::move(origin)} {
   if (!active_) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_descriptor, "db snapshot session is null");
   }
   const auto caps = active_->capabilities();
   if (!caps.snapshot_reads) {
      FORGE_THROW_EXCEPTION(exceptions::unsupported_operation, "db session does not support snapshot reads");
   }
}

bool snapshot::active() const noexcept {
   return static_cast<bool>(active_);
}

bool snapshot::belongs_to(const driver& owner) const noexcept {
   if (!origin_ || !owner.snapshot_origin_) {
      return false;
   }
   return !origin_.owner_before(owner.snapshot_origin_) &&
          !owner.snapshot_origin_.owner_before(origin_);
}

boost::asio::awaitable<std::optional<std::vector<std::byte>>> snapshot::get(family column_family, record_key key) {
   if (!active_) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db snapshot is closed");
   }
   co_return co_await active_->get(std::move(column_family), std::move(key));
}

boost::asio::awaitable<record_page> snapshot::scan_page(family column_family,
                                                        record_range range,
                                                        page_request request) {
   if (!active_) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed, "db snapshot is closed");
   }
   validate_page_request(request);
   co_return co_await active_->scan_page(std::move(column_family), std::move(range), std::move(request));
}

} // namespace forge::db::core
