# forge_blobdb

`forge_blobdb` is a neutral content-addressed blob store over a shared
`forge::db::driver`.

It owns blob mechanisms: put/get/has/stat/erase/verify, explicit
retain/release/ref counts and bounded collection of unreferenced blobs. It does
not own runtime GC scheduling, disk-pressure policy, hash choice, encryption,
erasure coding, manifests or product namespaces.

## Store

```cpp
auto driver = std::make_shared<forge::db::rocksdb::driver>(...);

forge::blobdb::store blobs{
   driver,
   forge::blobdb::store::config{
      .data_family = forge::db::family{"blobdb.data"},
      .refs_family = forge::db::family{"blobdb.refs"},
      .digest_hasher = product_hasher
   }};
```

The product supplies a hasher when it wants `put(bytes) -> digest`. The explicit
`put(digest, bytes)` path is always available and may verify the digest when a
hasher is configured.

## Shared Transactions

BlobDB can join any active `forge::db::transaction`, including one also used by
ObjectDB:

```cpp
auto tx = co_await driver->begin_transaction();
auto object_tx = objects.join(tx);
auto blob_tx = blobs.join(tx);

auto digest = co_await blob_tx.put(bytes);
co_await blob_tx.retain(digest, owner);
co_await object_tx.insert(metadata{.digest = digest});

co_await tx.commit();
```

Joined BlobDB transactions do not own commit/rollback. Standalone
`blobs.begin_transaction()` remains the convenience path.

## Retention

`retain`, `release`, `ref_count` and `collect_unreferenced(limit)` are library
mechanisms. Runtime policy such as when to collect, what owners are live, how
much to delete per pass and how to report metrics belongs to plugins or
products.
