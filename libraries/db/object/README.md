# forge_db_object

`forge_db_object` is the typed object/index engine layer for Forge. It owns
object descriptors, primary and secondary index maintenance, object
serialization, async transactions and stable read snapshots.

It is backend-free. Physical persistence is supplied by a shared
`forge::db::core::driver`, so DB Object can run over in-memory test drivers, RocksDB
through `forge_db_rocksdb`, and future backends without importing them.

## Layering

- `forge::db::core`: low-level record driver, transaction and snapshot contract.
- `forge::db::object`: typed objects, indexes, hooks and store/transaction API.
- `forge::db::rocksdb`: RocksDB implementation of the shared `forge::db`
  driver.
- `plugins.db.store`: application lifecycle and named physical DB stores with an
  optional object layer.

`forge_db_object` must not import `forge_rocksdb`, plugins, app lifecycle or
product code.

## Object Declaration

Objects derive from `forge::db::object::object<Derived, Space, Type>`. The base
owns `id` as `forge::ids::typed_id<Space, Type>` and contains no storage
behavior.

```cpp
struct account : forge::db::object::object<account, 1, 7> {
   std::string name;
   std::uint64_t balance = 0;
   std::uint32_t region = 0;
};

BOOST_DESCRIBE_STRUCT(account, (forge::db::object::object<account, 1, 7>), (name, balance, region))

struct by_id;
struct by_name;
struct by_region_balance;

using account_object = forge::db::object::object_index<
   account,
   forge::db::object::indexed_by<
      forge::db::object::primary_unique<by_id>,
      forge::db::object::secondary_unique<by_name, &account::name>,
      forge::db::object::secondary_non_unique<
         by_region_balance,
         forge::db::object::composite_key<&account::region, &account::balance>>>>;

FORGE_DB_OBJECT(account_object)
```

`object_index<T, indexed_by<...>>` is a schema descriptor, not a base class.
User values remain described C++ structs. `FORGE_DB_OBJECT(...)` creates
the compile-time mapping from `typed_id<Space, Type>` to the descriptor, so
typed-id operations do not require spelling the object type again.

## Store

`forge::db::object::store` receives a shared `forge::db::core::driver` and a DB Object
record family:

```cpp
auto driver = std::make_shared<forge::db::rocksdb::driver>(
   forge::db::rocksdb::config{
      .path = "data/rocksdb",
      .families = {"objectdb"}
   });

forge::db::object::store store{
   driver,
   forge::db::object::store::config{
      .family = forge::db::core::family{"objectdb"}
   }};

store.register_object<account_object>();
```

The default write policy is `single_writer`, which serializes DB Object
mutations at the store layer. `write_policy::backend` is available for drivers
that intentionally own write concurrency.

## Transactions And Direct Calls

All mutations use transaction semantics. Direct `store` calls open a short
transaction, delegate to the same mutation pipeline, commit on success and
rollback on failure:

```cpp
co_await store.insert(account{...});
auto created = co_await store.create<account>([](account& value) {
   value.name = "alice";
   value.balance = 100;
});
co_await store.replace(account{...});
co_await store.modify(account::id_t{42}, [](account& value) {
   value.balance += 100;
});
co_await store.erase(account::id_t{42});
```

`insert(value)` is strict and expects `value.id` to be already assigned. Use
`create<T>(...)` when DB Object should allocate a new ID. Generated IDs are
monotonic per `{space,type}`, start at instance `0`, and are not allocated again
after erase, rollback or failed insert; gaps are expected.

Use an explicit transaction when several object changes must commit together:

```cpp
auto tx = co_await store.begin_transaction();
auto created = co_await tx.create<account>([](account& value) {
   value.name = "bob";
});
co_await tx.modify(account::id_t{42}, [](account& value) {
   value.region = 3;
});
co_await tx.commit();
```

If an uncommitted transaction is destroyed, DB Object asks the underlying
`forge::db::core::transaction` to rollback best-effort. Explicit rollback propagates
backend rollback errors after cleanup.

## Shared Transactions

DB Object can join an existing `forge::db::core::transaction`. This is how DB Object and
DB Blob share one backend commit boundary:

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
auto value = co_await store.get(account::id_t{42});
auto maybe = co_await store.find(account::id_t{42});
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
failed commit. Hooks are DB Object-level and do not expose backend write batches.

## Modules

- `forge.db.object.object`: base object and descriptor mapping.
- `forge.db.object.index`: index declarations, views, range queries and streams.
- `forge.db.object.cursor`: DB Object pagination validation over
  `forge.db.core.record` request types.
- `forge.db.object.transaction`: high-level DB Object transaction.
- `forge.db.object.snapshot`: read-only snapshot view.
- `forge.db.object.hooks`: interceptors, observers and change sets.
- `forge.db.object.store`: store, direct wrappers and write policy.
- `forge.db.object.exceptions`: typed `forge.db.object` errors.
- `<forge/db/object/macros.hpp>`: macro-only typed-id mapping declaration.

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
