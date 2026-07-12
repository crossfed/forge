# Forge DB Revisions And Migrations

Status: future direction. This document records a candidate design and required
correctness properties. It does not declare shipped modules, targets or public
API names.

## Purpose

FORGE DB needs a reusable mechanism for durable revisions: a committed group of
database changes that can be inspected, reverted, reapplied and eventually
pruned.

The immediate downstream use case is a blockchain controller that must revert
already committed blocks during a fork switch. The same lower-level mechanism
can support a future `forge.db.migrations` layer, especially for migrations that
span multiple backend transactions and must resume safely after a crash.

The revision mechanism belongs in FORGE because atomic mutation capture,
ObjectDB index consistency, BlobDB reference lifetime and backend transaction
integration are framework concerns. Product code supplies revision identity and
policy; it must not reimplement a partial journal around FORGE DB.

## Current Surface

`forge.db.object.hooks` already provides two useful extension points:

- an `interceptor` receives each proposed `object_mutation` before ObjectDB
  writes the object and its indexes;
- an `observer` receives the final `change_set` after a successful commit.

These hooks remain useful, but neither is an atomic revision boundary.

### Interceptor Limitation

`interceptor::before_mutation(...)` receives a proposed semantic object change,
but does not receive:

- the active `forge::db::core::transaction`;
- a transaction or revision identity;
- the final transaction change set;
- commit or rollback notification;
- BlobDB mutations.

The callback also runs before unique-index verification and before physical
object/index writes. A later failure may therefore reject a mutation already
seen by the interceptor. An interceptor may validate or veto a mutation, but it
must not independently persist that mutation as a committed revision.

### Observer Limitation

`observer::after_commit(...)` receives only successfully committed ObjectDB
mutations. It is appropriate for cache invalidation, metrics, audit events and
eventual projections.

It is too late for the authoritative revision journal. A process can crash
after the backend commit and before the observer writes the journal, leaving
committed state without the information required to revert it.

### Transaction Limitation

`forge::db::core::transaction` currently supports `after_commit` and
`after_rollback` hooks, but has no prepare-before-commit participant. ObjectDB
keeps its in-progress `change_set` private until commit processing.

The missing boundary is a transaction-integrated prepare phase that can persist
the revision record through the same backend transaction as the application
changes.

## Required Separation

Revision journaling and schema migration are related but are not the same API.

### Revision Journal

A revision journal owns generic database mechanics:

- capture physical or semantic changes;
- atomically persist revision metadata and mutations;
- revert or reapply a committed revision;
- verify parent/head continuity;
- recover incomplete operations after restart;
- checkpoint and prune revisions;
- keep referenced BlobDB content alive until the revision is no longer
  reversible.

### Migration Layer

A future migration layer owns upgrade policy:

- schema and catalog versions;
- ordered migration identifiers;
- compatibility checks;
- maintenance-mode and online-migration policy;
- progress checkpoints;
- retry and resume rules;
- operator diagnostics;
- optional use of revisions for rollback.

A migration is not automatically reversible. Large data transformations may be
safer as idempotent forward-only steps with checkpoints and backups than as a
full before-image journal. Restoring database bytes also does not prove that an
older application binary can understand the restored schema.

## Candidate Layering

```text
forge.db.core
  backend transaction
  record-level mutation capture
  prepare/commit/rollback participants

forge.db.revision                 working name
  durable revision journal
  revert/reapply/checkpoint/prune
  ObjectDB and BlobDB integration

forge.db.migrations               future consumer
  schema catalog and migration runner
  resumable multi-step upgrades

downstream products               future consumers
  application revision identity and policy
  blockchain fork choice, migration selection, retention rules
```

The names `forge.db.revision` and `forge.db.migrations` are working names only.
They must go through normal library design before becoming public modules or
package components.

## Mutation Granularity

The authoritative journal should be grounded at `forge.db.core` record level,
because that is the common atomic boundary for ObjectDB, indexes, BlobDB and
future DB layers.

A conceptual record delta is:

```cpp
struct record_delta {
   family family;
   record_key key;
   std::optional<std::vector<std::byte>> before;
   std::optional<std::vector<std::byte>> after;
};
```

This is not a final API declaration. It illustrates the required information.

Record-level capture has important properties:

- ObjectDB primary records and secondary indexes are reverted together;
- schema migrations can restore old bytes without decoding them as the new
  object type;
- backend behavior can be tested independently from ObjectDB;
- revision replay does not silently depend on current index extractors.

ObjectDB `object_mutation` remains valuable as a semantic projection for
diagnostics and product logic. It should not be the only authoritative rollback
format across schema changes.

## BlobDB Requirements

Revision journaling must not copy every content-addressed blob into every
revision. It must preserve enough reference information to prevent collection
while a revision can still restore that blob.

The design must cover:

- blob creation and deduplication;
- owner `retain` and `release` transitions;
- reference counts before and after a revision;
- garbage collection barriers for reversible revisions;
- pruning a revision and releasing its retention obligation;
- crash recovery between metadata and blob-reference changes.

ObjectDB-only hooks cannot provide these guarantees.

## Candidate Transaction Flow

The intended semantic flow is:

```text
begin backend transaction
  -> mutate DB Core/Object/Blob through joined handles
  -> collect final record deltas
  -> prepare revision metadata and journal records
  -> write journal through the same backend transaction
  -> commit once
  -> publish post-commit observers
```

The transaction must not report success when application state committed but
the corresponding revision record did not.

A future API may use a transaction participant or an explicit revision scope.
The design must avoid recursive capture when journal records are written and
must define which internal families are excluded from their own journal.

## Revert And Reapply

Revert applies deltas in reverse order and swaps the direction:

```text
insert   -> erase inserted record
erase    -> restore previous record
replace  -> restore previous bytes
```

Reapply uses the recorded `after` values in forward order.

Required invariants:

- a revision has a stable opaque identity;
- an optional parent identifies the state on which it was built;
- revert is rejected when the active head does not match the revision;
- partial revert/reapply cannot become visible;
- retry after a crash is deterministic and idempotent;
- indexes and blob references remain consistent;
- unknown journal versions fail closed;
- corrupt deltas are detected before commit where possible.

Branch selection is not a FORGE responsibility. A blockchain controller may
use `block_id` as revision identity and `previous` as parent, but FORGE must not
contain block, fork-choice, finality or irreversible-block vocabulary.

## Relationship To Migrations

### Single-Transaction Migration

If a migration fits in one backend transaction, ordinary commit/rollback is
already sufficient for failure before commit. A durable revision is optional.

### Multi-Transaction Migration

Long migrations may require bounded batches. The migration layer then needs:

- a durable migration state record;
- last completed checkpoint;
- idempotent resume behavior;
- an explicit decision whether completed batches are reversible;
- compatibility rules while old and new layouts coexist.

Revision journaling can provide reversible batches and crash evidence, but the
migration runner still owns ordering and version policy.

### Destructive Migration

A migration that discards information must declare itself irreversible or
provide an external backup/restore plan. The revision layer must not imply that
all schema changes have a safe automatic downgrade.

## Non-Goals

- No blockchain controller, fork choice or finality logic in FORGE.
- No product schema or object models in the revision library.
- No automatic rollback promise for every migration.
- No journal written by an `after_commit` observer in a second transaction.
- No stateful global interceptor that guesses transaction boundaries.
- No raw RocksDB API in public revision or migration contracts.
- No unbounded retention of revisions or blob content.
- No final module, target or namespace commitment in this planning document.

## Implementation Blocks

### Block 1: Core Transaction Participation

- define backend-neutral mutation capture;
- define a prepare-before-commit participant;
- prove atomic participant writes with application records;
- define rollback behavior when prepare fails;
- prevent nested or recursive journal capture.

### Block 2: Durable Revisions

- define versioned revision metadata and delta encoding;
- implement commit, load, revert and reapply;
- implement head/parent continuity checks;
- implement checkpoint and bounded pruning;
- add corruption and crash-recovery tests.

### Block 3: ObjectDB And BlobDB Integration

- preserve existing ObjectDB semantic `change_set` observers;
- map ObjectDB/index records into the core revision atomically;
- journal BlobDB owner-reference transitions;
- prevent garbage collection of content required by reversible revisions;
- prove behavior through shared test-driver and RocksDB suites.

### Block 4: Migration Catalog And Runner

- define schema/catalog version records;
- define migration identifiers and dependency ordering;
- implement transactional and checkpointed migration modes;
- implement resume after process termination;
- make rollback capability explicit per migration;
- expose operator-readable progress and failure diagnostics.

## Test Requirements

The implementation is not production-ready until the same behavior suite passes
against a deterministic test driver and RocksDB:

- application records and journal commit atomically;
- prepare failure leaves neither data nor journal changes;
- process termination at every commit boundary recovers deterministically;
- insert/replace/erase revert and reapply round-trip byte-identically;
- ObjectDB secondary indexes survive revert/reapply;
- BlobDB retain/release and garbage collection remain correct;
- stale-parent and wrong-head operations are rejected;
- journal corruption and unsupported versions fail closed;
- pruning cannot remove the active head or required blob content;
- a multi-batch migration resumes from its last durable checkpoint;
- irreversible migrations cannot be invoked through a rollback path.

## Open Questions

- Should mutation capture be implemented by DB Core directly or by a required
  driver transaction capability?
- What is the minimal backend-neutral representation of column-family identity?
- Should revision identity be opaque bytes, a digest, or a typed application
  value encoded into bytes?
- How should large before-images be bounded, compressed or externalized?
- How are journal schema upgrades performed without making the journal unable to
  restore older records?
- Which retention policies belong in the generic library and which must be
  supplied by applications?
- Does a migration runner need online dual-layout support in its first version,
  or should v1 require maintenance mode?

These questions must be resolved from concrete donor behavior and failure tests,
not from API naming preferences alone.
