# forge_objectdb

`forge_objectdb` is the typed object/index engine layer for Forge. It owns
object descriptors, primary and secondary index maintenance, object
serialization, async transactions and stable read snapshots.

It is backend-free. Physical persistence is supplied by a shared
`forge::db::driver`, so ObjectDB can run over in-memory test drivers, RocksDB
through `forge_db_rocksdb`, and future backends without importing them.

## Layering

- `forge::db`: low-level record driver, transaction and snapshot contract.
- `forge::objectdb`: typed objects, indexes, hooks and store/transaction API.
- `forge::db::rocksdb`: RocksDB implementation of the shared `forge::db`
  driver.
- `plugins.db.objectdb`: application lifecycle and named object stores.

`forge_objectdb` must not import `forge_rocksdb`, plugins, app lifecycle or
product code.

## Object Declaration

Objects derive from `forge::objectdb::object<Derived, Space, Type>`. The base
owns `id` as `forge::ids::typed_id<Space, Type>` and contains no storage
behavior.

```cpp
struct account : forge::objectdb::object<account, 1, 7> {
   std::string name;
   std::uint64_t balance = 0;
   std::uint32_t region = 0;
};

BOOST_DESCRIBE_STRUCT(account, (forge::objectdb::object<account, 1, 7>), (name, balance, region))

struct by_id;
struct by_name;
struct by_region_balance;

using account_object = forge::objectdb::object_index<
   account,
   forge::objectdb::indexed_by<
      forge::objectdb::primary_unique<by_id>,
      forge::objectdb::secondary_unique<by_name, &account::name>,
      forge::objectdb::secondary_non_unique<
         by_region_balance,
         forge::objectdb::composite_key<&account::region, &account::balance>>>>;

FORGE_OBJECTDB_OBJECT(account_object)
```

`object_index<T, indexed_by<...>>` is a schema descriptor, not a base class.
User values remain described C++ structs. `FORGE_OBJECTDB_OBJECT(...)` creates
the compile-time mapping from `typed_id<Space, Type>` to the descriptor, so
typed-id operations do not require spelling the object type again.

## Store

`forge::objectdb::store` receives a shared `forge::db::driver` and an ObjectDB
record family:

```cpp
auto driver = std::make_shared<forge::db::rocksdb::driver>(
   forge::db::rocksdb::config{
      .path = "data/rocksdb",
      .families = {"objectdb"}
   });

forge::objectdb::store store{
   driver,
   forge::objectdb::store::config{
      .family = forge::db::family{"objectdb"}
   }};

store.register_object<account_object>();
```

The default write policy is `single_writer`, which serializes ObjectDB
mutations at the store layer. `write_policy::backend` is available for drivers
that intentionally own write concurrency.

## Transactions And Direct Calls

All mutations use transaction semantics. Direct `store` calls open a short
transaction, delegate to the same mutation pipeline, commit on success and
rollback on failure:

```cpp
co_await store.insert(account{...});
co_await store.replace(account{...});
co_await store.modify(account::id_type{42}, [](account& value) {
   value.balance += 100;
});
co_await store.erase(account::id_type{42});
```

Use an explicit transaction when several object changes must commit together:

```cpp
auto tx = co_await store.begin_transaction();
co_await tx.insert(account{...});
co_await tx.modify(account::id_type{42}, [](account& value) {
   value.region = 3;
});
co_await tx.commit();
```

If an uncommitted transaction is destroyed, ObjectDB asks the underlying
`forge::db::transaction` to rollback best-effort. Explicit rollback propagates
backend rollback errors after cleanup.

## Shared Transactions

ObjectDB can join an existing `forge::db::transaction`. This is how ObjectDB and
BlobDB share one backend commit boundary:

```cpp
auto db_tx = co_await driver->begin_transaction();

auto object_tx = objects.join(db_tx);
auto blob_tx = blobs.join(db_tx);

auto digest = co_await blob_tx.put(bytes);
co_await object_tx.insert(metadata{.digest = digest});

co_await db_tx.commit();
```

`store.begin_transaction()` is the convenience owning path. `store.join(tx)` is
the shared path and does not own commit/rollback.

## Reads And Indexes

Direct reads use `begin_read()` and stable backend snapshots:

```cpp
auto value = co_await store.get(account::id_type{42});
auto maybe = co_await store.find(account::id_type{42});
```

Index queries are Boost.MultiIndex-style and execute through persisted ordered
index records:

```cpp
auto alice = co_await store.index<account_object, by_name>().find("alice");

auto page = co_await store.index<account_object, by_region_balance>()
   .equal_range(std::make_tuple(std::uint32_t{3}))
   .page({.limit = 100});

auto stream = store.index<account_object, by_region_balance>()
   .equal_range(std::make_tuple(std::uint32_t{3}))
   .stream({.page_size = 100});
```

Streams keep one read snapshot for the whole stream lifecycle.

## Hooks

Interceptors run before mutation writes and may veto. Observers run after a
successful commit with a `change_set`; they are not called for rollback or
failed commit. Hooks are ObjectDB-level and do not expose backend write batches.

## Modules

- `forge.objectdb.object`: base object and descriptor mapping.
- `forge.objectdb.index`: index declarations, views, range queries and streams.
- `forge.objectdb.record`: compatibility re-exports of public record value
  types from `forge.db.record`.
- `forge.objectdb.cursor`: cursor/page request aliases and validation.
- `forge.objectdb.transaction`: high-level ObjectDB transaction.
- `forge.objectdb.snapshot`: read-only snapshot view.
- `forge.objectdb.hooks`: interceptors, observers and change sets.
- `forge.objectdb.store`: store, direct wrappers and write policy.
- `forge.objectdb.exceptions`: typed `forge.objectdb` errors.
- `<forge/objectdb/macros.hpp>`: macro-only typed-id mapping declaration.

Deterministic key layout and record materialization are private implementation
details.

## Migration Groundwork

Runtime migration/catalog support is intentionally out of this block. These are
migration events for a future explicit migration layer:

- changing `{space, type}`;
- changing index order or index kind;
- changing an extractor or composite-key member order;
- changing base object serialization;
- changing the ordered key codec.
