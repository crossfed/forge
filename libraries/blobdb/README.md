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

## References

`forge::blobdb::ref<Digest>` is the typed value used by metadata layers and APIs
when they need to store both the content digest and the blob size:

```cpp
forge::blobdb::sha256_ref value{
   .digest = forge::crypto::sha256::hash(payload),
   .size = payload.size()
};
```

`sha256_ref` is an alias for `ref<forge::crypto::sha256>`. Other digest types can
participate by specializing `forge::blobdb::digest_traits<Digest>` with byte and
text conversion functions. The current byte-oriented `forge::blobdb::digest`
also has a traits specialization for compatibility with the storage engine.

Variant/JSON-friendly conversion is a string in the form `<digest-text>:<size>`,
for example `<sha256-hex>:1234`. Binary raw serialization stays compact and
typed: digest bytes followed by the `uint64` size. It does not serialize through
the text form.

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
