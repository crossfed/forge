# Forge DB State Services v1

Status: architecture triage. This document classifies DB mechanisms adjacent to
savepoints and durable revisions. It separates required revision work from
independent read, checkpoint and migration concerns so one implementation branch
does not silently become an unbounded database framework rewrite.

Related designs:

- [Forge DB Savepoints v1](forge-db-savepoints-v1.md);
- [Forge DB Revisions v1](forge-db-revisions-v1.md);
- [Forge DB Revision And Migration Boundary](forge-db-revisions-migrations-v1.md).

## Purpose

FORGE DB already provides a shared transaction boundary for DB Core, DB Object
and DB Blob. Savepoints and revisions add partial rollback and durable committed
undo history.

Four adjacent mechanisms require an explicit decision:

1. a shared high-level read view over one Core snapshot;
2. durable checkpoint, export and restore;
3. schema/data migration orchestration;
4. DB Store plugin integration for revisions.

They are not one feature. They have different consumers, backend requirements,
failure modes and donor patterns.

## Classification

| Mechanism | Generic value | Immediate requirement | Recommended ownership | Current decision |
| --- | --- | --- | --- | --- |
| Shared read view | Consistent Object/Blob/Core reads | Strong for metadata/content runtimes; optional for many state-machine consumers | DB Core snapshot plus Object/Blob read views | Design accepted in principle, implementation separate unless branch scope remains small |
| Physical checkpoint | Local openable backend copy | Useful for backup, recovery and rapid local bootstrap | Separate DB checkpoint capability/library | Separate design and PR |
| Logical export/import | Portable typed state artifact | Product-dependent | Product/chain state layer over DB iteration | Not a DB Core feature |
| Generic migration runner | Ordered resumable schema/data upgrades | Not yet proven | Future DB migration library | Deferred pending real requirements and donors |
| Revision plugin layer | Runtime configuration/access to DB Revision | Required for runtime configuration, but not for the library contract | `plugins.db.store` optional layer | Separate follow-up after the revision library stabilizes |

## 1. Shared High-Level Read View

### Current Surface

`forge::db::core::driver::begin_read()` returns one backend snapshot and
`forge::db::object::store::begin_read()` returns an Object snapshot.

The missing composition surface is:

- DB Object cannot join an externally supplied Core snapshot through `store`;
- DB Blob has no read-only snapshot/view type;
- DB Store plugin cannot open one snapshot and expose all configured layers over
  it.

Independent calls may therefore observe different committed points.

### Candidate Library API

```cpp
auto read = co_await driver->begin_read();

auto object_view = objects.join(read);
auto blob_view = blobs.join(read);

auto metadata = co_await object_view.get(file_id);
auto payload = co_await blob_view.get(metadata.content);
```

Required additions:

```cpp
forge::db::object::snapshot
forge::db::object::store::join(forge::db::core::snapshot)

forge::db::blob::snapshot
forge::db::blob::store::join(forge::db::core::snapshot)
```

The Core snapshot may be copied as a lightweight shared-session handle. Every
joined view verifies that its store uses the same driver ownership and that the
backend reports `snapshot_reads` capability.

### Blob Snapshot Surface

A DB Blob snapshot is read-only. It may expose:

- `get(ref)`;
- `has(ref)`;
- `stat_blob(ref)`;
- `verify(ref)`;
- `ref_count(ref)` when owner-reference records share the snapshot.

It must not expose:

- `put`;
- `erase`;
- `retain` or `release`;
- `collect_unreferenced`;
- commit or rollback.

### DB Object Snapshot Surface

The existing Object snapshot already owns typed reads and indexes. The new join
path must use the same registration checks and record/index decoding as
`begin_read()`; it must not introduce a second read implementation.

### When A Blockchain Consumer Needs It

A state transition does not require a read snapshot because execution already
uses one shared write transaction.

Many immutable-data reads also do not require a shared snapshot:

- metadata contains a content digest/reference;
- content at that digest is immutable;
- the consumer does not inspect mutable owner/reference state;
- retention guarantees that referenced content remains available.

Under those conditions, reading metadata and then fetching immutable content by
digest remains semantically correct even if the two operations do not share one
snapshot.

A shared read view becomes useful when:

- one response combines several mutable state tables at exactly one point;
- metadata and retention/reference state must agree;
- concurrent collection could remove a payload after metadata was read;
- an API requires repeatable multi-page reads;
- diagnostics must prove a point-in-time state relationship.

Therefore a shared high-level read view is useful but not a universal
blockchain execution prerequisite.

### Why It Matters To Storlane-Like Runtimes

A metadata/content runtime performs continuous namespace lookups and content
reads while metadata, cache state and synchronization state change concurrently.
A bounded shared read view can preserve one mapping from metadata to immutable
content across a compound operation.

It is particularly useful for:

- lookup followed by metadata/content resolution;
- directory enumeration with related metadata reads;
- consistent cache-fill planning;
- diagnostics over metadata and retention state;
- read paths that must not mix pre-update and post-update records.

An indefinitely held RocksDB snapshot is not acceptable. Long-lived snapshots
retain old versions and can increase compaction and disk pressure. The API must
encourage operation-scoped or bounded-batch views, not one snapshot for the
lifetime of a daemon or mount.

### Shared Read View In The Plugin

If implemented, DB Store may add a read wrapper:

```cpp
auto read = co_await handle.begin_read();
auto object_view = handle.objects().join(read);
auto blob_view = handle.blobs().join(read);
```

The wrapper owns one `forge::db::core::snapshot`. It does not own lifecycle of
the physical store and becomes invalid after plugin close.

### Required Tests

- Object and Blob views read from one Core snapshot;
- a concurrent commit is invisible to both joined views;
- independently opened views may observe different points;
- content deleted after snapshot creation remains readable through the snapshot
  when the backend guarantees snapshot visibility;
- wrong driver/store joins fail with a typed error;
- unsupported backends reject before returning a partial view;
- no mutation API is reachable through Blob/Object read views;
- repeated open/close does not leak backend snapshots.

### Decision

The API shape is generic and useful. It should be a separate focused change from
savepoint/revision unless implementation proves limited to thin reuse of the
existing Core and Object snapshot paths.

It is not required to validate the revision design.

## 2. Durable Checkpoint, Export And Restore

The word snapshot is currently overloaded. Three distinct artifacts must not be
combined under one API.

### Read Snapshot

An in-process point-in-time read view:

- tied to an open backend;
- not portable;
- not a backup;
- released when the view dies.

This is the existing `forge::db::core::snapshot` concern.

### Physical Checkpoint

An openable copy of backend files at one committed point:

- backend-specific representation;
- normally local filesystem oriented;
- useful for backup, local restore and fast local bootstrap;
- compatible only with supported backend/storage versions.

RocksDB's `Checkpoint` utility creates this artifact. It hard-links SST and Blob
files on the same filesystem, copies them otherwise, and copies the required
manifest files. This is a strong donor for a RocksDB physical-checkpoint
implementation, not for a portable logical state format.

### Logical Export

A versioned record/domain artifact:

- portable across backend instances;
- may be streamable and checksummed;
- requires explicit schemas and section ordering;
- can include product state not owned by DB;
- needs compatibility transforms during import.

Spring snapshots are logical chain/controller snapshots. Managers explicitly
write and read typed sections, and the controller owns compatibility behavior.
They are not a generic Chainbase filesystem checkpoint and should not be copied
into DB Core.

### Proposed Ownership

Physical checkpoint support should be a separate DB family component, working
name:

```text
target     forge_db_checkpoint
component  db_checkpoint
namespace  forge::db::checkpoint
modules    forge.db.checkpoint.*
```

The neutral contract may describe destination, backend/storage identity,
manifest, creation result and compatibility diagnostics. Raw
`rocksdb::Checkpoint` remains inside the RocksDB backend.

Restore is not an operation on an active transaction. It replaces or creates a
database before the driver opens. The final design must involve a driver
factory/physical-store lifecycle boundary, not `core::transaction::restore()`.

Logical product exports should live above DB:

- a chain/state layer may export protocol/system sections;
- a storage product may export namespace, content and journal metadata;
- DB supplies consistent iteration and optional physical checkpoint mechanics;
- DB does not invent product manifests.

### Failure And Safety Requirements

- destination is not silently overwritten;
- manifest is written last or atomically published;
- partial artifacts differ from complete checkpoints;
- format/backend compatibility is checked before restore/open;
- restore uses staging plus atomic directory promotion where supported;
- writer ownership and flush/WAL requirements are explicit;
- Blob files and all configured families are included;
- checkpoint deletion is explicit and not an automatic retention policy.

### Decision

Checkpoint/export/restore does not belong in the savepoint/revision
implementation branch. Add a donor-backed design and separate PR after revision
state is available.

## 3. DB Migrations

### Donor Finding

Spring does not provide a reusable general migration runner suitable for direct
porting into FORGE DB.

Its current pattern is:

- persist a database header version;
- validate that the version is within a supported range;
- reject incompatible state;
- require a compatible logical snapshot or replay;
- handle older snapshot layouts in chain-specific snapshot readers.

This is a compatibility gate and rebuild/restore strategy, not:

- an ordered migration catalog;
- a dependency graph;
- a resumable batch runner;
- a generic rollback policy;
- an operator-visible migration state machine.

Migration correctness spans crash recovery, version skew, partial progress and
downgrade policy. It should not be invented merely because revisions exist.

### What FORGE Keeps Now

- persisted format version in every system table/header;
- minimum/current compatible versions;
- typed incompatible-version errors;
- fail-closed open behavior;
- logical export or replay as an explicit recovery option;
- DB Revision as an optional mechanism, not an automatic migration runner.

### Deferred Work

- `forge_db_migrations` target/component;
- migration ID and dependency model;
- online versus maintenance-mode policy;
- multi-transaction checkpoints;
- forward-only versus reversible declarations;
- automatic downgrade;
- cross-version binary orchestration.

### Reopening The Decision

A generic migration library should be reconsidered only when a real consumer
provides two concrete persisted layouts, an actual upgrade operation,
restart/failure requirements, operational constraints and a proven donor or
production pattern.

Until then, the existing
[Revision And Migration Boundary](forge-db-revisions-migrations-v1.md) remains
documentation, not a planned implementation target.

### Decision

Do not implement a generic migration runner in the current branch.

## 4. DB Store Plugin Revision Integration

The DB Store plugin is the runtime aggregate for one named physical store. It
already owns one Core driver and optional Object/Blob layers. DB Revision is an
optional fourth layer.

### Configuration

```yaml
plugins:
  db:
    store:
      stores:
        - name: "state"
          driver: "rocksdb"
          path: "./data/state"
          object:
            family: "objectdb"
            write-policy: "single-writer"
          blob:
            data-family: "blobdb.data"
            refs-family: "blobdb.refs"
          revision: {}
```

Presence of `revision:` enables the layer. A separate `enabled` flag is
unnecessary.

Revision uses the configured Object family for its system tables:

- `revision:` requires `object:`;
- it does not configure another revision column family;
- it uses the same Core driver;
- it coordinates with Blob when Blob is enabled;
- retention policy is not inferred from YAML in v1.

Programmatic `store_options` gains an optional revision-layer option with the
same validation.

### Managed Store State

One named managed store owns:

```text
shared_ptr<db::core::driver>
optional shared_ptr<db::object::store>
optional shared_ptr<db::blob::store>
optional shared_ptr<db::revision::store>
```

Lifecycle snapshots copy all enabled handles under the plugin mutex. Revision
handle access follows the same stopping grace window and close boundary as
Object and Blob.

### Public Handle

```cpp
auto state = co_await db->store("state");

auto tx = co_await state.begin_transaction();
auto revision = co_await state.revisions().join(tx);
auto objects = co_await state.objects().join(tx);
auto blobs = state.blobs().join(tx);

co_await objects.insert(metadata);
co_await blobs.retain(content, owner);
co_await tx.commit();
```

`store_handle` adds:

```cpp
revision_handle revisions() const;
```

The revision handle wraps `forge::db::revision::store` and exposes revision
operations without product terminology.

### Explicit Revision Scope

`store_handle::begin_transaction()` must not automatically create a revision.
Not every maintenance, setup or internal transaction belongs in durable undo
history.

Callers explicitly opt in through `revisions().join(tx)`. An optional
`begin_revision()` convenience may be considered later, but it must delegate to
the same shared transaction implementation and add no policy.

### Lifecycle

- configuration validates that revision has Object support;
- `after_initialize()` opens Object, then Revision before startup;
- Revision system models become readable through the same Object handle;
- no storage I/O is allowed in the registration-only ready phase;
- startup only publishes runtime availability;
- stopping handles remain valid until shutdown closes stores;
- shutdown closes logical layers before releasing the driver;
- flush remains one driver operation per named store.

### Tests

- config accepts object+revision and rejects revision without object;
- programmatic options enforce the same rule;
- Object system tables are registered during open;
- `objects()` reads revision state types;
- `revisions()` without the layer throws a typed unavailable error;
- joined revision commits Object and Blob changes atomically;
- rollback writes no revision;
- savepoint rollback removes pending revision deltas;
- stopping and post-close handle contracts remain unchanged;
- RocksDB reopen preserves revision state and Blob retention.

### Decision

Plugin revision integration is a separate follow-up after the DB Revision
library stabilizes. The library implementation must not depend on plugin
configuration or lifecycle, and this document preserves the intended runtime
shape for that later PR.

## Proposed Scope Of The Current Branch

### Required In This Branch

1. DB Core savepoints and participant state machine.
2. RocksDB native savepoint mapping.
3. DB Object/Blob savepoint participation.
4. DB Revision system models and system-registration support.
5. Core mutation capture and prepare-before-commit.
6. Revision commit, head-only revert and bounded prune.
7. Object ID and Blob retention correctness.

### Separate Follow-Up, Not A Revision Blocker

1. Shared high-level read view over one Core snapshot.
2. DB Store optional revision layer.

Neither mechanism is implemented in the savepoint/revision branch. They retain
their own public API, lifecycle and review boundaries.

### Separate Future PR

1. Physical checkpoint capability and RocksDB implementation.
2. Restore/factory lifecycle.
3. Any product logical export/import format.

### Deferred

1. Generic DB migration runner.

## Donor Summary

### Spring/Chainbase

Useful for revision/undo semantics, typed logical snapshot sections and system
database version validation.

Not a donor for generic DB migration orchestration, backend-neutral physical
checkpoints or FORGE shared Object/Blob read views.

### RocksDB

Useful for native read snapshots, transaction savepoints, physical openable
checkpoints and conflict/storage-lifetime behavior.

It is not sufficient for Object system tables, Blob retention policy, logical
product export formats or generic migration semantics.

## Final Boundary

```text
transactions and read snapshots       DB Core
typed objects/system tables           DB Object
immutable content and ownership       DB Blob
partial transaction rollback          DB Core Savepoints
durable committed undo history        DB Revision
runtime store composition             plugins.db.store
physical local backup                 future DB Checkpoint
portable product state artifact       product/chain state layer
schema/data upgrade orchestration      deferred DB Migrations
```

No one layer should absorb the others merely because a downstream runtime uses
all of them.
