# forge_db_revision

`forge_db_revision` is the durable undo-history layer for the Forge DB family.
It captures before-images from one active `forge::db::core::transaction`, commits
the application changes and journal atomically, and can later revert only the
current committed revision.

## Package

- CMake target: `forge_db_revision`
- package component: `db_revision`
- namespace: `forge::db::revision`
- modules: `forge.db.revision.types`, `forge.db.revision.transaction`,
  `forge.db.revision.store` and `forge.db.revision.exceptions`

The library depends on `forge_db_core` and `forge_db_object`. It has no direct
dependency on DB Blob; Blob participates through the neutral DB Core participant
contract.

## Revisions

Open the revision store over the same driver and Object store that own the
application state:

```cpp
auto objects = co_await forge::db::object::store::open(driver);
auto revisions = co_await forge::db::revision::store::open(driver, objects);

auto tx = co_await revisions.begin_transaction();
co_await tx.db_transaction().put(records, key, value);
const auto candidate = tx.id();
co_await tx.commit();
```

Revision IDs are Forge-owned monotonic `std::uint64_t` values starting at `1`.
An uncommitted candidate may be issued again after rollback. A committed ID is
never reused. One Core transaction can own at most one revision scope, and the
scope must be joined before its first mutation or savepoint.

The journal stores before-images, not after-images. A no-op transaction still
commits an entry with zero deltas. Journal state, entries and deltas are DB
Object system models, so they can be read and indexed through the normal
read-only Object API while application mutation APIs cannot modify them.

## Shared Transactions

`join()` attaches revision capture to a caller-owned transaction:

```cpp
auto tx = co_await driver->begin_transaction();
auto revision = co_await revisions.join(tx);
auto object_tx = objects.join(tx);
auto blob_tx = blobs.join(tx);

co_await object_tx.insert(metadata);
co_await blob_tx.retain(content, owner);
co_await tx.commit();
```

Savepoint rollback discards the corresponding pending revision deltas. Savepoint
release merges them into the outer revision. Generated Object IDs remain
consumed even when their object mutations are rolled back to a savepoint.

## Revert

Revert is a new durable transaction, not a rollback of the original transaction:

```cpp
auto tx = co_await driver->begin_transaction();
co_await revisions.revert(tx, expected_head);
co_await tx.commit();
```

Only the current head can be reverted. The implementation validates the complete
entry and contiguous delta range before changing application records, applies
before-images in reverse order, removes the reverted journal rows and moves the
head to its parent. It never decrements the next revision ID.

## Blob Retention

Blob owner refs are reversible. When a revision removes an owner ref, DB Blob
creates an internal retention barrier so `collect_unreferenced()` cannot delete
the payload required by a future revert. The barrier is not visible through
public `ref_count()` and is removed atomically by revert or prune.

Blob payload puts are excluded from the journal: after revert, a newly written
payload may become unreferenced and can be collected later. Payload erase and
explicit collection are forbidden while a revision scope is active.

## Bounded Prune

`prune_through()` removes retained history in whole-revision batches:

```cpp
auto tx = co_await driver->begin_transaction();
auto result = co_await revisions.prune_through(
   tx,
   boundary,
   {.max_revisions = 100, .max_deltas = 10'000});
co_await tx.commit();
```

The boundary must be older than the active head. A call never removes part of a
revision. If the first revision exceeds `max_deltas`, it fails without mutations.
Commit each batch and repeat until `result.complete` is true.

## Boundaries

The driver must provide record locks. Revision branches, reapply, historical
checkout, automatic Blob collection policy, shared Object/Blob read views,
migrations, physical checkpoints and plugin integration are outside this
library.
