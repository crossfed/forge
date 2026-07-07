# forge_blobdb

`forge_blobdb` is a neutral content-addressed blob store over a shared
`forge::db::driver`.

It owns blob mechanisms: put/get/has/stat/erase/verify, explicit
retain/release/ref counts and bounded collection of unreferenced blobs. It does
not own runtime GC scheduling, disk-pressure policy, encryption, erasure coding,
manifests or product namespaces.

## Store

```cpp
auto driver = std::make_shared<forge::db::rocksdb::driver>(...);

forge::blobdb::store blobs{
   driver,
   forge::blobdb::store::config{
      .data_family = forge::db::family{"blobdb.data"},
      .refs_family = forge::db::family{"blobdb.refs"}
   }};
```

The default `put(bytes)` path uses `forge::crypto::sha256` and returns
`forge::blobdb::sha256_ref`:

```cpp
auto content = co_await blobs.put(payload);
// content.digest == forge::blobdb::hash<forge::blobdb::digest>{}(payload)
// content.size   == payload.size()
```

Alternative digest algorithms do not require a templated store. Provide
`forge::blobdb::hash<Digest>` and `forge::blobdb::digest_traits<Digest>`, then
use the thin typed wrappers:

```cpp
auto content = co_await blobs.put_as<product_digest>(payload);
co_await blobs.verify(content);
```

## References

`forge::blobdb::ref<Digest>` is the typed value used by metadata layers and APIs
when they need to store both the content digest and the blob size:

```cpp
forge::blobdb::sha256_ref value{
   .digest = forge::blobdb::hash<forge::blobdb::digest>{}(payload),
   .size = payload.size()
};
```

`forge::blobdb::digest` is the default digest type and aliases
`forge::crypto::sha256`. `sha256_ref` is an alias for `ref<digest>`. Other digest
types can participate by specializing `forge::blobdb::hash<Digest>` and
`forge::blobdb::digest_traits<Digest>` with hashing, byte conversion, text
conversion and a stable algorithm id.

Variant/JSON-friendly conversion is a string in the form `<digest-text>:<size>`,
for example `<sha256-hex>:1234`. Binary raw serialization stays compact and
typed: fixed-size digests are encoded as digest bytes followed by the `uint64`
size; variable-size digests add a binary `uint32` digest length before the digest
bytes. It does not serialize through the text form.

BlobDB private keys include the digest algorithm id and digest bytes, so two
algorithms that produce the same bytes do not collide in data or ref records.

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
