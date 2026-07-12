module;

#include <forge/exceptions/macros.hpp>

#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rocksdb/iterator.h>
#include <rocksdb/utilities/transaction_db.h>

module forge.rocksdb.store;

import forge.exceptions;
import forge.rocksdb.exceptions;

#include "details/native.hxx"
#include "details/snapshot_impl.hxx"
#include "details/store_impl.hxx"

namespace forge::rocksdb {

snapshot::snapshot(std::unique_ptr<impl> impl_value) : impl_{std::move(impl_value)} {}
snapshot::~snapshot() = default;
snapshot::snapshot(snapshot&&) noexcept = default;
snapshot& snapshot::operator=(snapshot&&) noexcept = default;

void snapshot::ensure_active(std::string_view context) const {
   if (impl_ == nullptr || impl_->snapshot == nullptr) {
      FORGE_THROW_EXCEPTION(exceptions::invalid_argument, std::string{context} + ": RocksDB snapshot is closed");
   }
}

std::optional<std::vector<std::byte>>
snapshot::get(family column_family, std::vector<std::byte> key, read_options options) {
   ensure_active("failed to get RocksDB snapshot value");
   std::string value;
   const auto status = impl_->store->db->Get(
      detail::to_native_options(options, impl_->snapshot),
      impl_->store->require_handle(column_family),
      detail::to_slice(key),
      &value);
   if (status.IsNotFound()) {
      return std::nullopt;
   }
   detail::throw_if_error(status, "failed to get RocksDB snapshot value");
   auto bytes = std::vector<std::byte>{};
   bytes.resize(value.size());
   std::memcpy(bytes.data(), value.data(), value.size());
   return bytes;
}

std::vector<entry> snapshot::scan(family column_family,
                                  std::vector<std::byte> prefix,
                                  read_options options) {
   ensure_active("failed to scan RocksDB snapshot prefix");
   auto iterator = std::unique_ptr<::rocksdb::Iterator>{
      impl_->store->db->NewIterator(
         detail::to_native_options(options, impl_->snapshot),
         impl_->store->require_handle(column_family)),
   };

   auto values = std::vector<entry>{};
   for (iterator->Seek(detail::to_slice(prefix)); iterator->Valid(); iterator->Next()) {
      auto key = detail::bytes_from_slice(iterator->key());
      if (!detail::starts_with(key, prefix)) {
         break;
      }
      values.push_back(entry{.key = std::move(key), .value = detail::bytes_from_slice(iterator->value())});
   }
   detail::throw_if_error(iterator->status(), "failed to scan RocksDB snapshot prefix");
   return values;
}

scan_result snapshot::scan_page(family column_family, scan_request request) {
   ensure_active("failed to scan RocksDB snapshot prefix page");
   auto iterator = std::unique_ptr<::rocksdb::Iterator>{
      impl_->store->db->NewIterator(
         detail::to_native_options(request.options, impl_->snapshot),
         impl_->store->require_handle(column_family)),
   };
   return detail::read_scan_page(
      std::move(iterator), std::move(request), "failed to scan RocksDB snapshot prefix page");
}

} // namespace forge::rocksdb
