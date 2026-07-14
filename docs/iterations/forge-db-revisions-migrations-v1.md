# Forge DB Revision And Migration Boundary

Status: future migration direction. The canonical durable revision design is
defined separately in [Forge DB Revisions v1](forge-db-revisions-v1.md).
Transaction-local partial rollback is defined in
[Forge DB Savepoints v1](forge-db-savepoints-v1.md).

## Purpose

DB revisions and schema migrations may cooperate, but they are separate
mechanisms with different ownership.

- DB Revision records a committed state transition and can restore its
  before-images later.
- DB Savepoint undoes an uncommitted suffix of one active transaction.
- DB Migration owns schema/version upgrade policy, ordering, checkpoints and
  operational diagnostics.

A migration may use revisions or savepoints, but neither mechanism decides
which migration should run or whether an old binary remains compatible with the
restored schema.

## Accepted Layering

```text
forge.db.core
  transactions, savepoints, mutation participants

forge.db.object
  application and Forge system object models

forge.db.revision
  durable system tables, before-images, revert and prune

future forge.db.migrations
  schema catalog, ordered migration runner and resumable checkpoints
```

The revision library is not part of DB Object even though its system tables are
typed DB Object models. It captures the shared Core transaction so Object,
indexes, Blob references and direct Core records remain one atomic revision.

## Migration Responsibilities

A future migration layer owns:

- persisted schema/catalog versions;
- stable migration identifiers;
- dependency and execution ordering;
- compatibility and maintenance-mode checks;
- bounded batch size;
- durable progress checkpoints;
- retry and restart behavior;
- operator-readable status and errors;
- declaration of reversible versus forward-only operations;
- optional use of DB Revision for reversible batches.

These responsibilities do not belong to the revision journal.

## Single-Transaction Migration

If a migration fits in one backend transaction, ordinary commit/rollback already
protects failure before commit.

Savepoints may isolate optional steps inside that transaction. A durable revision
is optional and is useful only when the committed migration must be reversible
afterward.

```text
begin transaction
  create savepoint
  apply optional conversion
  rollback/release savepoint
  apply required conversion
commit
```

## Multi-Transaction Migration

A large migration may require bounded batches. It then needs a durable migration
state record containing at least:

- migration identifier and format version;
- current phase;
- last completed checkpoint;
- batch progress;
- retry/error state;
- completion marker.

Each batch is independently atomic. A DB revision may make an individual batch
reversible, but it does not make the entire multi-transaction migration atomic.
The runner must resume deterministically after interruption.

## Destructive Migration

A migration that discards information must either:

- declare itself irreversible;
- preserve the required information in DB Revision before-images; or
- require an external backup/restore procedure.

The migration API must never imply automatic downgrade merely because a revision
mechanism exists. Restoring old bytes does not prove that an older application
binary can understand every surrounding catalog or storage-format change.

## Revision Use By Migrations

When enabled for a migration batch:

```text
begin Core transaction
  join DB Revision scope
  apply migration writes
  persist migration checkpoint
prepare revision journal
commit once
```

The migration checkpoint, transformed records and revision journal commit in the
same backend transaction.

Rolling back the open transaction requires no revision. Reverting a previously
committed batch uses the separate revision operation and must still be authorized
by migration policy.

## System Tables

Migration catalog objects may also use DB Object's system-object mechanism, but
they must have their own reserved system type IDs and persisted-format version.

Revision and migration tables are separate:

- revision state describes reversible committed transitions;
- migration state describes upgrade intent and progress;
- pruning revisions must not delete migration completion history;
- deleting migration history must not release revision Blob retention barriers.

## Non-Goals

- No automatic schema inference.
- No assumption that every migration is reversible.
- No product-specific upgrade policy.
- No journal written after commit in a second transaction.
- No unbounded revision retention.
- No direct RocksDB API in migration contracts.
- No final `forge.db.migrations` API commitment before donor and failure-mode
  design is completed.

## Future Design Questions

- Does v1 require maintenance mode, or support online dual-layout migration?
- How are migration dependencies and skipped versions represented?
- Which batch progress fields are generic across backends?
- How does a migration declare its revision and backup requirements?
- How are system-table format migrations bootstrapped safely?
- Which operations require exclusive DB ownership?
- How are abandoned and operator-cancelled migrations recovered?

These questions remain migration work. They must not reopen the accepted
savepoint or revision ownership boundaries.
