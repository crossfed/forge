module;

#include <forge/exceptions/macros.hpp>

#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module forge.db.blob.snapshot;

import forge.db.blob.exceptions;

#include "details/read.hxx"
#include "details/snapshot_impl.hxx"

namespace forge::db::blob {

snapshot::impl::impl(forge::db::core::snapshot active_value,
                     forge::db::core::family data,
                     forge::db::core::family refs) noexcept
    : active{std::move(active_value)},
      data_family{std::move(data)},
      refs_family{std::move(refs)} {}

snapshot::snapshot(forge::db::core::snapshot active,
                   forge::db::core::family data_family,
                   forge::db::core::family refs_family)
    : impl_{std::make_shared<impl>(
         std::move(active), std::move(data_family), std::move(refs_family))} {}

bool snapshot::active() const noexcept {
   return impl_ && impl_->active.active();
}

boost::asio::awaitable<std::vector<std::byte>> snapshot::get_encoded(
   std::string algorithm,
   std::vector<std::byte> digest,
   std::uint64_t size) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed,
                            "db blob snapshot is closed");
   }
   co_return co_await detail::read_payload(
      impl_->active, impl_->data_family, algorithm, digest, size);
}

boost::asio::awaitable<bool> snapshot::has_encoded(
   std::string algorithm,
   std::vector<std::byte> digest,
   std::uint64_t size) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed,
                            "db blob snapshot is closed");
   }
   co_return co_await detail::has_payload(
      impl_->active, impl_->data_family, algorithm, digest, size);
}

boost::asio::awaitable<stat> snapshot::stat_blob_encoded(
   std::string algorithm,
   std::vector<std::byte> digest,
   std::uint64_t size) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed,
                            "db blob snapshot is closed");
   }
   co_return co_await detail::read_stat(
      impl_->active,
      impl_->data_family,
      impl_->refs_family,
      algorithm,
      digest,
      size);
}

boost::asio::awaitable<std::uint64_t> snapshot::ref_count_encoded(
   std::string algorithm,
   std::vector<std::byte> digest) {
   if (!active()) {
      FORGE_THROW_EXCEPTION(exceptions::transaction_closed,
                            "db blob snapshot is closed");
   }
   co_return co_await detail::count_refs(
      impl_->active, impl_->refs_family, algorithm, digest);
}

} // namespace forge::db::blob
