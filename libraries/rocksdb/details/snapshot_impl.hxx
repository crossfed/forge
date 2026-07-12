#pragma once

namespace forge::rocksdb {

struct snapshot::impl {
   impl(std::shared_ptr<store::impl> store_value, const ::rocksdb::Snapshot* snapshot_value);
   ~impl();

   std::shared_ptr<store::impl> store;
   const ::rocksdb::Snapshot* snapshot = nullptr;
};

} // namespace forge::rocksdb
