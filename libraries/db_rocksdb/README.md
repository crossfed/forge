# forge_db_rocksdb

`forge_db_rocksdb` implements the shared `forge::db::driver` contract over
`forge::rocksdb::store`.

## Scope

- Maps `forge::db::family` to RocksDB column families.
- Opens RocksDB write transactions for `driver.begin_transaction()`.
- Opens RocksDB snapshots for `driver.begin_read()`.
- Preserves synchronous `flush(bool)` and exposes async `async_flush(bool)`.

It does not contain ObjectDB or BlobDB semantics. Those libraries use this
driver through the neutral `forge_db` contract.

## Config

```cpp
forge::db::rocksdb::driver driver{
   forge::db::rocksdb::config{
      .path = "data/rocksdb",
      .families = {
         forge::rocksdb::column_family_config{"objectdb"},
         forge::rocksdb::column_family_config{"blobdb.refs"},
      }
   }
};
```

Families are `forge::rocksdb::column_family_config`, so callers can enable
RocksDB blob files for blob-heavy families without changing ObjectDB or BlobDB.
