module;

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <rocksdb/db.h>
#include <rocksdb/utilities/transaction_db.h>

module forge.rocksdb.store;

#include "details/snapshot_impl.hxx"
#include "details/store_impl.hxx"

namespace forge::rocksdb {

snapshot::impl::impl(std::shared_ptr<store::impl> store_value,
                     const ::rocksdb::Snapshot* snapshot_value)
   : store{std::move(store_value)}, snapshot{snapshot_value} {}

snapshot::impl::~impl() {
   if (store && store->db && snapshot != nullptr) {
      store->db->ReleaseSnapshot(snapshot);
   }
}

} // namespace forge::rocksdb
