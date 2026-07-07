module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

module forge.db.driver;

import forge.db.exceptions;

namespace forge::db {

snapshot::snapshot(std::unique_ptr<session> active) : active_{std::move(active)} {
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

} // namespace forge::db
