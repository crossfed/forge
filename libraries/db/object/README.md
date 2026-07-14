# forge_db_object

`forge_db_object` is the typed object/index engine layer for Forge. It owns
object descriptors, primary and secondary index maintenance, object
serialization, async transactions and stable read snapshots.

## Stability

The DB Object public C++ API is **Preview** in Forge 8.x. It may receive
documented source-incompatible refinements in a MINOR release. Persisted object
and index layouts are a separate compatibility boundary and are not made
unstable by this source API status.

It is backend-free. Physical persistence is supplied by a shared
`forge::db::core::driver`, so DB Object can run over in-memory test drivers, RocksDB
through `forge_db_rocksdb`, and future backends without importing them.

## Layering

- `forge::db::core`: low-level record driver, transaction and snapshot contract.
- `forge::db::object`: typed objects, indexes, hooks and store/transaction API.
- `forge::db::rocksdb`: RocksDB implementation of the shared
  `forge::db::core::driver` contract.
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
      forge::db::object::ordered_unique<
         by_name,
         forge::db::object::member<&account::name>>,
      forge::db::object::ordered_non_unique<
         by_region_balance,
         forge::db::object::composite_key<
            forge::db::object::member<&account::region>,
            forge::db::object::descending<
               forge::db::object::member<&account::balance>>>>>>;

FORGE_DB_OBJECT(account_object)
```

`object_index<T, indexed_by<...>>` is a schema descriptor, not a base class.
User values remain described C++ structs. `FORGE_DB_OBJECT(...)` specializes
`forge::db::object::index_for_id` with the compile-time mapping from
`typed_id<Space, Type>` to the descriptor, so typed-id operations do not require
spelling the object type again. The macro declares no `forge::ids` symbols and
does not perform runtime registration.

Ordered descriptors follow the Boost.MultiIndex separation between index kind
and key extraction. `member`, `const_mem_fun` and `global_fun` extract scalar
keys; `composite_key` combines two or more extractors. Extractors are ascending
unless wrapped in `descending`; `ascending` can make the default explicit.

Index values use `sort_key<T>` to produce canonical ascending bytes. Forge
ships codecs for booleans, integers, enums, strings, object IDs, typed IDs and
fixed-byte values exposing `to_uint8_span()` or `extract_as_byte_array()`.
Products can support strong domain types without changing DB Object:

```cpp
template <>
struct forge::db::object::sort_key<domain_float> {
   forge::db::object::sort_key_bytes operator()(const domain_float& value) const;
};
```

The specialization owns normalization and invalid-value policy. For example, a
SoftFloat consumer decides how signed zero and NaN values behave. Codec failures
surface as `forge::db::object::exceptions::invalid_index_key` before index or
object records are mutated.

## Store

`forge::db::object::store` receives a shared `forge::db::core::driver` and a DB Object
record family:

```cpp
auto driver = std::make_shared<forge::db::rocksdb::driver>(
   forge::db::rocksdb::config{
      .path = "data/rocksdb",
      .families = {"objectdb"}
   });

auto store = co_await forge::db::object::store::open(
   driver,
   forge::db::object::store::config{
      .family = forge::db::core::family{"objectdb"}
   });

store.register_object<account_object>();
```

`open()` validates or creates the persisted DB Object header before returning.
It rejects non-empty families without a header and versions outside the
supported range. The cached header is available without I/O through
`store.header()`.

## System Objects

Object space `0` is reserved for Forge system objects. Application objects must
use a non-zero space. The built-in `forge::db::object::header` is stored at
`{space=0,type=0,instance=0}` and records the DB Object persisted-format
version.

System objects are ordinary typed models for reads and indexes:

```cpp
import forge.db.object.header;

const auto cached = store.header();
const auto persisted = co_await store.get(forge::db::object::header_id);
```

They derive from `system_object<Derived, Type>`. Public `insert`, `create`,
`replace`, `modify` and `erase` accept only application objects, so attempts to
mutate the header are rejected at compile time. Bootstrap and future migration
code use a private path that does not invoke application interceptors or
observers.

`forge.db.object.system` owns the central catalog of reserved system type IDs
and the infrastructure-only access path used by DB family libraries. DB
Revision uses this path for its state, entry and delta models. Those rows remain
readable through normal Object reads and indexes but cannot be mutated through
application write APIs.

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

auto object_tx = co_await objects.join(db_tx);
auto blob_tx = blobs.join(db_tx);

auto digest = co_await blob_tx.put(bytes);
co_await object_tx.insert(metadata{.digest = digest});

co_await db_tx.commit();
```

`store.begin_transaction()` is the convenience owning path. `store.join(tx)` is
the shared path and does not own commit/rollback. Joining is asynchronous:
`write_policy::single_writer` waits for the store writer lane and keeps it until
the outer Core transaction commits, rolls back or is dropped. Joining an
existing Object transaction from the same store reuses its participant and
returns another non-owning facade; it never acquires a second writer ticket.
`write_policy::backend` delegates serialization to the backend and does not wait
on the Object writer lane.

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
   .equal_range(std::uint32_t{3})
   .page({.limit = 100});

auto stream = store.index<account_object, by_region_balance>()
   .equal_range(std::tuple{std::uint32_t{3}})
   .stream({.page_size = 100});

auto exact = co_await store.index<account_object, by_region_balance>()
   .find(std::uint32_t{3}, std::uint64_t{100});

auto tail = store.index<account_object, by_region_balance>()
   .lower_bound(std::uint32_t{3}, std::uint64_t{100});
```

`find` requires a complete composite key. `equal_range`, `lower_bound` and
`upper_bound` accept a non-empty ordered prefix. Variadic and tuple forms use
the same key encoder. Streams keep one read snapshot for their whole lifecycle.

## Hooks

Interceptors run before mutation writes and may veto. Observers run after a
successful commit with a `change_set`; they are not called for rollback or
failed commit. Hooks are DB Object-level and do not expose backend write batches.

## Modules

- `forge.db.object.object`: base object and descriptor mapping.
- `forge.db.object.header`: persisted format header and its system descriptor.
- `forge.db.object.system`: reserved system type catalog and infrastructure
  registration/access contract.
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

## Index Roadmap

DB Object currently supports primary unique, ordered unique and ordered
non-unique indexes, including composite keys and mixed ascending/descending
components. Hashed indexes are deferred: persisted ordered backends already
provide exact and range access, while a backend-neutral hash contract needs a
separate demonstrated use case. Boost.MultiIndex `sequenced` and
`random_access` indexes do not map to this persisted object/index model. Ranked
indexes require a separate backend-neutral rank-maintenance design.

## Migration Groundwork

Runtime migration/catalog support is intentionally out of this block. These are
migration events for a future explicit migration layer:

- changing `{space, type}`;
- changing index order or index kind;
- changing an extractor or composite-key member order;
- changing base object serialization;
- changing the ordered key codec.

The candidate transaction-integrated revision journal and its boundary with a
future migration runner are documented in
[`docs/iterations/forge-db-revisions-migrations-v1.md`](../../../docs/iterations/forge-db-revisions-migrations-v1.md).
