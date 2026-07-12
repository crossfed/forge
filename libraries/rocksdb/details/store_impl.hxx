#pragma once

namespace forge::rocksdb {

struct store::impl {
   explicit impl(config value);
   ~impl();

   [[nodiscard]] ::rocksdb::ColumnFamilyHandle* require_handle(const family& column_family) const;

   config settings;
   std::unique_ptr<::rocksdb::TransactionDB> db;
   std::unordered_map<std::string, ::rocksdb::ColumnFamilyHandle*> handles;
};

} // namespace forge::rocksdb
