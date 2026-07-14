# forge_db_blob

`forge_db_blob` is a neutral content-addressed blob store over a shared
`forge::db::core::driver`.

It owns blob mechanisms: put/get/has/stat/erase/verify, explicit
retain/release/ref counts and bounded collection of unreferenced blobs. It does
not own runtime GC scheduling, disk-pressure policy, encryption, erasure coding,
manifests or product namespaces.

## Store

```cpp
auto driver = std::make_shared<forge::db::rocksdb::driver>(...);

forge::db::blob::store blobs{
   driver,
   forge::db::blob::store::config{
      .data_family = forge::db::core::family{"blobdb.data"},
      .refs_family = forge::db::core::family{"blobdb.refs"}
   }};
```

The default `put(bytes)` path uses `forge::crypto::sha256` and returns
`forge::db::blob::ref<forge::db::blob::digest>`:

```cpp
auto content = co_await blobs.put(payload);
// content.digest == forge::db::blob::hash<forge::db::blob::digest>{}(payload)
// content.size   == payload.size()
```

Alternative digest algorithms do not require a templated store. Provide
`forge::db::blob::hash<Digest>` and `forge::db::blob::digest_traits<Digest>`, then
use the thin typed wrappers:

```cpp
auto content = co_await blobs.put_as<product_digest>(payload);
co_await blobs.verify(content);
```

## References

`forge::db::blob::ref<Digest>` is the typed value used by metadata layers and APIs
when they need to store both the content digest and the blob size:

```cpp
forge::db::blob::ref<forge::db::blob::digest> value{
   .digest = forge::db::blob::hash<forge::db::blob::digest>{}(payload),
   .size = payload.size()
};
```

`forge::db::blob::digest` is the default digest type and aliases
`forge::crypto::sha256`. Other digest types can participate by specializing `forge::db::blob::hash<Digest>` and
`forge::db::blob::digest_traits<Digest>` with hashing, byte conversion, text
conversion and a stable algorithm id.

Variant/JSON-friendly conversion is a string in the form `<digest-text>:<size>`,
for example `<sha256-hex>:1234`. Binary raw serialization stays compact and
typed: fixed-size digests are encoded as digest bytes followed by the `uint64`
size; variable-size digests add a binary `uint32` digest length before the digest
bytes. It does not serialize through the text form.

DB Blob private keys include the digest algorithm id and digest bytes, so two
algorithms that produce the same bytes do not collide in data or ref records.

## Shared Transactions

DB Blob can join any active `forge::db::core::transaction`, including one also used by
DB Object:

```cpp
auto tx = co_await driver->begin_transaction();
auto object_tx = co_await objects.join(tx);
auto blob_tx = blobs.join(tx);

auto digest = co_await blob_tx.put(bytes);
co_await blob_tx.retain(digest, owner);
co_await object_tx.insert(metadata{.digest = digest});

co_await tx.commit();
```

Joined DB Blob transactions do not own commit/rollback. Standalone
`blobs.begin_transaction()` remains the convenience path.

## Retention

`retain`, `release`, `ref_count` and `collect_unreferenced(limit)` are library
mechanisms. Runtime policy such as when to collect, what owners are live, how
much to delete per pass and how to report metrics belongs to plugins or
products.

When a DB Revision participant is attached, owner-ref changes remain
reversible. Removing an owner ref creates an internal retention barrier when a
future revert may need the payload. Barriers are not included in public
`ref_count()`, but the collector honors them. Revert or prune removes the
barrier atomically with the corresponding revision history.

Payload puts are excluded from revision history. Payload erase and explicit
collection are forbidden inside an active revision scope, because physically
removing content required by a before-image would make revert incomplete.
Automatic garbage collection remains outside the library.
