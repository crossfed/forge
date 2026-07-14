# forge_db_core

`forge_db_core` is the shared low-level record driver layer. It owns the public
record-oriented contract used by higher-level stores such as DB Object and DB
Blob.

## Scope

- `forge::db::core::record_key`, `record_range`, `record_entry`, `record_page`,
  `cursor`, `page_request` and `family`.
- `forge::db::core::session`: backend-owned async record session.
- `forge::db::core::driver`: opens write transactions and read snapshots.
- `forge::db::core::transaction`: move-only commit/rollback boundary with
  savepoints, optional record locks and participant hooks.
- `forge::db::core::snapshot`: stable read-only view.

`forge_db_core` does not know about objects, blobs, RocksDB, plugins, paths, WAL
policy or product schemas.

## Driver Contract

A backend implements `forge::db::core::driver` by opening sessions with honest
capabilities:

- `snapshot_reads=false, writes=true`: write transaction session.
- `snapshot_reads=true, writes=false`: read-only stable snapshot.
- `snapshot_reads=true, writes=true`: universal session.
- `snapshot_reads=false, writes=false`: invalid and rejected.

`transaction` owns commit/rollback and participant cleanup hooks. Higher-level
libraries can join the same transaction and share one backend commit boundary.

## Savepoints And Participants

Savepoints are transient LIFO boundaries inside one active transaction:

```cpp
auto tx = co_await driver->begin_transaction();

co_await tx.put(records, key_a, value_a);
const auto point = co_await tx.create_savepoint();
co_await tx.put(records, key_b, value_b);

co_await tx.rollback_to_savepoint(point);
co_await tx.commit();
```

Only the top savepoint can be rolled back or released, and either operation
consumes it. Rolling back a savepoint removes its suffix of changes while the
outer transaction remains active. Releasing it keeps the changes and removes
only the boundary. Outer commit and rollback close every remaining frame.

Backends advertise savepoint and record-lock support through `capabilities`.
Unsupported operations, stale or non-top IDs, ID overflow and operations on a
rollback-only transaction fail with typed DB Core exceptions.

Higher-level DB libraries attach `transaction_participant` implementations to
keep their in-memory state aligned with native savepoints and final
commit/rollback. Participants are retained by the Core transaction, prepare
before commit and classify record mutations as reversible, excluded or
forbidden. This contract is implementer-facing; savepoints themselves do not
create durable revisions.

## Families

`forge::db::core::family` is a logical record space inside one driver. RocksDB maps it
to column families. In-memory drivers can map it to separate ordered maps.
