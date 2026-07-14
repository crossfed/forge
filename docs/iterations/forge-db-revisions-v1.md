# Forge DB Revisions v1

Status: implemented. This document defines the shipped ownership boundaries,
persisted model and correctness invariants for the first durable revision
implementation. It does not describe product policy.

## Purpose

A DB revision is a durable description of one successfully committed database
state transition. It allows a later transaction to restore the state that
existed before that transition.

Revisions solve a different problem from transaction rollback:

- `transaction::rollback()` discards changes that have not committed;
- `rollback_to_savepoint()` discards an uncommitted suffix while keeping the
  transaction active;
- `revision::revert()` restores already committed state by applying persisted
  before-images through a new transaction.

The mechanism is generic DB infrastructure. Its public vocabulary must not
contain block, fork, consensus, finality or other downstream product terms.

## Decision Summary

- Revisions are a separate DB family library, not methods on DB Object.
- Target: `forge_db_revision`.
- Package component: `db_revision`.
- Namespace: `forge::db::revision`.
- Public module prefix: `forge.db.revision`.
- The library depends on DB Core and DB Object.
- It does not depend directly on DB Blob.
- One revision scope observes one shared `forge::db::core::transaction`.
- Object, index, Blob-reference and direct Core changes are captured at the
  shared Core transaction boundary.
- Revision metadata and deltas are DB Object system objects in the configured
  Object store.
- Consumers read revision tables through the ordinary read/index surface of
  `forge::db::object::store`.
- Public application mutations cannot insert, modify or erase revision system
  objects.
- Revision journal writes commit atomically with the state changes they
  describe.
- Journal writes are excluded from their own mutation capture.
- Revision identity is a scalar:

  ```cpp
  using revision_id_t = std::uint64_t;
  ```

- Forge assigns committed revision IDs monotonically.
- Reverting a revision never decreases or reuses the committed-ID high-water
  mark.
- v1 stores before-images and supports linear `revert` plus bounded pruning.
- Physical Blob payload deletion is forbidden while a revision scope is
  attached; Blob owner references remain reversible and revision-owned
  retention barriers protect payloads required by retained history.
- Pruning removes only complete revisions within explicit revision/delta
  limits. It never persists a partially deleted revision.
- v1 does not promise reapply, branch storage or arbitrary historical checkout.
- Savepoints remain transaction-local and do not create revisions.

## Why Chainbase Places Revision Methods On Its Database

Chainbase `database` owns every registered index. Its undo session creates one
index session per index and fans `push`, `squash` and `undo` across the complete
set. Its revision methods therefore live on the object database because that
class is also the aggregate storage boundary.

FORGE has a different ownership model:

```text
forge::db::core::driver
  physical backend and shared transaction boundary

forge::db::object::store
  one logical object family and its indexes

forge::db::blob::store
  Blob data and owner-reference families

future DB layers
  additional logical record spaces over the same driver
```

`forge::db::object::store` does not own Blob or arbitrary Core writes. Placing
revision control directly on Object store would either miss those changes or
force DB Object to depend on every optional DB layer.

The donor behavior is therefore preserved at the equivalent FORGE boundary:
the shared Core transaction plus a dedicated revision participant. The donor's
exact class placement is not copied when the class responsibilities differ.

## Library Boundary

The planned physical layout follows the DB family structure:

```text
libraries/db/revision/
  CMakeLists.txt
  README.md
  include/forge/db/revision/
    types.cppm
    store.cppm
    transaction.cppm
    exceptions.cppm
  details/
    store_impl.hxx
    transaction_impl.hxx
  store.cpp
  store_impl.cpp
  transaction.cpp
  transaction_impl.cpp
```

Exact files may be reduced if a component contains only declarations or value
types. Implementation must follow `create-library`: no dummy sources, no empty
aggregate module and exact `.cppm/.hxx` to `.cpp` ownership.

### Dependency Direction

```text
forge_db_core
  ^
  |
forge_db_object
  ^
  |
forge_db_revision

forge_db_blob -> forge_db_core
```

DB Revision may use DB Object's system-object infrastructure. DB Object must not
import DB Revision. DB Blob does not import DB Revision and DB Revision does not
import DB Blob.

Optional Blob integration occurs through the shared Core transaction
participant/capture contract, avoiding a target cycle.

## Separation Of Control And Read Model

Revision operations belong to `forge::db::revision::store`:

- begin or join a revision scope;
- inspect the current revision head;
- revert the active head revision;
- prune retained history;
- validate revision continuity and journal format.

Persisted rows are DB Object system models. Once `revision::store` is opened for
an Object store, those models are registered internally and can be read through
normal Object APIs.

This separation provides both required properties:

- revision control covers every participating DB layer;
- revision state remains visible as typed system tables through DB Object.

It does not add `revision()`, `undo()` or `commit(revision)` methods to
`forge::db::object::store`.

## Opening The Revision Layer

Conceptual construction:

```cpp
auto objects = co_await forge::db::object::store::open(
   driver,
   {.family = forge::db::core::family{"objectdb"}});

auto revisions = co_await forge::db::revision::store::open(
   driver,
   objects);
```

Both handles refer to the same driver and physical database. `revision::store`
retains a copy of the lightweight Object store handle and installs its system
models through a narrow DB Object system-registration mechanism.

Opening must reject:

- a null driver;
- Object store and revision store backed by different driver ownership;
- incompatible DB Object or revision persisted-format versions;
- conflicting system object type assignments;
- an existing revision table with missing or corrupt state;
- a non-empty revision catalog that lacks its mandatory header/state rows.

## Public Revision Identity

```cpp
using revision_id_t = std::uint64_t;
```

No wrapper struct is introduced solely to hold this scalar.

The persisted state owns `next_revision`. A new revision scope reserves the
current value inside the same outer transaction and increments the stored next
value.

Only successfully committed entries are revisions. If the outer transaction
rolls back before a revision is committed, no revision with that ID exists. A
later transaction may therefore receive that candidate value. This is not reuse
of a committed revision.

Once a revision commits:

- its ID is never assigned to another committed revision;
- `next_revision` never decreases;
- revert does not return the ID to a free list;
- pruning does not make the ID reusable;
- overflow is detected before journal or application state changes.

## Persisted System Tables

Revision rows use DB Object's reserved system space. Type identifiers must be
allocated from a central Forge system-object type catalog so independently
developed system libraries cannot collide.

The following shapes are conceptual public value types. Final field names and
index declarations must preserve these semantics.

### Revision State

One singleton stores catalog state:

```cpp
struct state : forge::db::object::system_object<state, state_type> {
   std::uint32_t format_version = 1;
   revision_id_t next_revision = 1;
   std::optional<revision_id_t> head;
   std::optional<revision_id_t> prune_baseline;
   std::optional<revision_id_t> oldest_retained;
   std::uint64_t next_delta = 0;
};
```

Required meaning:

- `next_revision` is the next candidate committed revision ID;
- `head` is the newest active reversible revision;
- `prune_baseline` is the newest committed revision whose undo data has been
  deliberately removed;
- `oldest_retained` is the oldest revision whose before-images remain;
- `next_delta` allocates stable system-object IDs for delta rows;
- `format_version` gates decoding and migration.

### Revision Entry

One entry describes one committed revision:

```cpp
struct entry : forge::db::object::system_object<entry, entry_type> {
   std::optional<revision_id_t> parent;
   std::uint64_t delta_count = 0;
};
```

The inherited object ID instance equals the logical revision ID. `parent`
identifies the previous active revision, not an external domain object.

An entry has no product timestamp, producer, block identifier or policy field.
Applications may persist correlation metadata in their own tables if required.

### Revision Delta

Deltas are separate rows, not one unbounded vector inside the entry:

```cpp
struct delta : forge::db::object::system_object<delta, delta_type> {
   revision_id_t revision = 0;
   std::uint64_t ordinal = 0;
   forge::db::core::family family;
   forge::db::core::record_key key;
   std::optional<std::vector<std::byte>> before;
};
```

Required indexes include:

- primary system object ID;
- unique composite `(revision, ordinal)`;
- optional non-unique revision lookup if the composite implementation does not
  already provide an efficient prefix range.

Separate rows allow bounded page scans, bounded deletion during prune and
corruption checks against `delta_count`.

## Reading Through DB Object

Revision system models are readable through the Object store passed to
`revision::store::open()`:

```cpp
const auto catalog = co_await objects.get(
   forge::db::revision::state_id);

const auto current = co_await objects.find(
   forge::db::revision::entry::id_t{catalog.head.value()});

auto deltas = objects.index<
   forge::db::revision::delta_index,
   forge::db::revision::by_revision>();
```

DB Object's public mutation templates continue to accept application objects
only. Application code cannot use ordinary `insert`, `create`, `replace`,
`modify` or `erase` to corrupt revision system rows.

The revision library writes system objects through a narrow privileged
infrastructure path. That path:

- accepts only `system_object_model` types;
- bypasses application interceptors and observers;
- still uses normal DB Object encoding and index maintenance;
- requires the active shared Core transaction;
- is not a general application mutation escape hatch.

## Revision Transaction Flow

The external transaction remains the atomic owner:

```cpp
auto tx = co_await driver->begin_transaction();

auto revision_tx = co_await revisions.join(tx);
auto object_tx = co_await objects.join(tx);
auto blob_tx = blobs.join(tx);

co_await object_tx.insert(metadata);
auto content = co_await blob_tx.put(payload);
co_await blob_tx.retain(content, owner);

const auto candidate = revision_tx.id();
co_await tx.commit();
```

`revisions.join(tx)` is asynchronous because it must read and validate persisted
revision state before assigning the candidate ID and parent.

The returned revision transaction is a non-owning facade:

- it does not own final commit or rollback;
- `id()` is a candidate until outer commit succeeds;
- it registers mutation capture and savepoint participation;
- it persists journal rows during prepare-before-commit;
- it publishes no revision if the outer transaction rolls back.

An optional owning convenience path may return a revision transaction that owns
the Core transaction and whose `commit()` returns the committed ID. It must
delegate to the same implementation and must not establish a second semantic
path.

## Atomic Prepare Boundary

An `after_commit` observer cannot own the authoritative journal. The process can
terminate after application data commits and before a second journal transaction
does.

The Core transaction therefore needs a prepare-before-commit participant stage:

```text
mutate application/Core/Object/Blob records
  -> freeze mutation capture
  -> normalize and validate collected deltas
  -> write revision state, entry and delta system objects
  -> commit the one backend transaction
  -> run post-commit observers
```

If journal preparation fails, outer commit fails and the backend transaction is
rolled back. Application state and revision rows are never committed separately.

## Preventing Recursive Capture

Revision table writes occur in the same Object family as application objects,
but must not journal themselves.

The participant uses an explicit phase:

```text
capturing -> preparing -> closed
```

Only mutations observed during `capturing` become deltas. Entering `preparing`
freezes the accumulator. System-object writes made while persisting the journal
are therefore excluded without relying on string prefixes or backend-specific
column-family behavior.

Any attempt to perform an application mutation after capture is frozen is a
transaction state error. Prepare participants execute in deterministic order.

## Mutation Capture Level

The authoritative boundary is DB Core record mutation, because this includes:

- DB Object primary records;
- every secondary index record;
- DB Blob owner-reference records;
- direct DB Core records;
- records owned by future DB layers.

Object interceptors are too early and see only proposed semantic mutations.
Object observers are too late and run after commit. Neither can provide an
atomic cross-layer revision journal.

Core capture records the original value on the first mutation of a key within
the revision and coalesces later writes.

### Delta Coalescing

For one `(family, key)`:

- absent -> value -> another value stores `before = null`;
- value A -> B -> C stores `before = A`;
- value A -> erased -> D stores `before = A`;
- absent -> value -> erased is a net no-op and emits no final delta;
- value A -> B -> A is a net no-op and emits no final delta.

The final value is available from the active transaction during normalization,
but v1 does not persist it as an `after` field.

## Capture Policy And Non-Revertible State

Blindly reverting every physical record would violate DB-layer invariants.
Joined layers must classify internal mutations through the Core participant
contract.

### Default Core Records

Direct Core `put` and `erase` operations are reversible by default unless the
family/key is explicitly registered as internal non-revertible state.

### DB Object Records

Reversible:

- application object records;
- application secondary index records.

Not reverted:

- DB Object header/version rows;
- object ID allocation high-water records;
- revision system objects;
- internal coordination records whose monotonicity is a correctness boundary.

Generated object IDs remain consumed when a revision is reverted. Reverting an
object create removes the object and its indexes but does not make its ID
available again.

### DB Blob Records

Reversible:

- owner-reference transitions;
- metadata required to restore logical ownership.

Special handling:

- content-addressed payload bytes are not copied into every revision delta;
- payload `put` is excluded from capture and a reverted new payload may remain
  unreferenced for later explicit collection;
- payload `erase` and `collect_unreferenced` are rejected with a typed error
  while a revision scope is attached;
- a revision that may need an existing payload establishes an internal retention
  barrier;
- pruning or reverting the revision releases that barrier at the correct atomic
  boundary;
- explicit collection cannot remove payload required by a retained revision.

This integration is provided by DB Blob's transaction participant behavior, not
by a direct `forge_db_revision -> forge_db_blob` dependency.

## Savepoint Integration

Savepoints and revisions have different lifetimes but share participant
infrastructure. See [Forge DB Savepoints v1](forge-db-savepoints-v1.md).

When a revision scope is attached:

- `create_savepoint()` opens a nested accumulator frame;
- `rollback_to_savepoint()` discards that frame's mutations;
- `release_savepoint()` merges the frame into its parent;
- outer rollback discards every frame and writes no revision;
- outer commit normalizes all surviving frames into one revision.

Savepoints do not allocate revision IDs and do not create durable entry rows.
One outer transaction produces at most one revision regardless of how many
savepoints it used.

Coalescing across nested frames preserves the earliest before-image. For
example, A -> B before a savepoint and B -> C after it becomes:

- A -> B if the savepoint rolls back;
- A -> C if the savepoint releases and the outer transaction commits.

## Linear History Model

v1 maintains one active head and a parent chain.

Creating a revision:

```text
id = state.next_revision
parent = state.head
state.next_revision += 1
state.head = id
```

All state and journal writes commit atomically.

The revision store must serialize head updates for stores sharing the same
driver and Object family inside one process. The backend must also reject or
serialize conflicting state-row updates. A revision cannot commit on a stale
head.

Branch selection, multiple active heads and arbitrary historical checkout are
not v1 responsibilities.

## Revert

Revert restores the before-images of the active head through a new Core
transaction.

Conceptual library-only flow:

```cpp
auto tx = co_await driver->begin_transaction();
co_await revisions.revert(tx, expected_head);
co_await tx.commit();
```

The explicit commit is required because revert performs new writes. Until that
transaction commits, no restored state is durable or visible outside the
transaction.

Required algorithm:

1. read and lock/validate revision state;
2. require `state.head == expected_head`;
3. load exactly `entry.delta_count` deltas;
4. validate ordinals, family/key encoding and journal version;
5. apply deltas in reverse ordinal order;
6. restore `before` bytes with `put`, or erase when `before` is absent;
7. apply Blob retention transitions;
8. delete the reverted entry and its delta rows;
9. set `state.head = entry.parent`;
10. leave `state.next_revision` unchanged;
11. commit once through the caller's Core transaction.

Revert only accepts the current head. Reverting an older entry while newer
revisions remain would overwrite later state and is rejected with a typed
`not_head` or continuity exception.

Revert journal writes and restoration writes run with ordinary revision capture
disabled. Revert does not automatically create another revision describing its
own inverse.

## Pruning

Pruning removes the ability to revert old committed revisions without changing
the current application state.

Conceptual API:

```cpp
auto result = co_await revisions.prune_through(
   tx,
   retained_boundary,
   {
      .max_revisions = 64,
      .max_deltas = 4096,
   });
co_await tx.commit();
```

Required behavior:

- require explicit positive `max_revisions` and `max_deltas` limits;
- delete only complete revisions, starting with the oldest retained revision;
- stop before the first revision that would exceed either limit;
- fail without mutation if the first candidate revision alone exceeds
  `max_deltas`;
- delete or compact corresponding entry metadata;
- release Blob retention barriers owned only by pruned revisions;
- advance `prune_baseline` and `oldest_retained` atomically;
- never change `head` or `next_revision`;
- never prune data required to revert a retained revision;
- reject boundaries at or newer than head or inconsistent with the retained
  chain;
- return progress and whether the requested boundary has been completed;
- require the caller to commit each complete bounded batch before requesting
  the next one.

No partial-revision cursor is persisted in v1. Every successful call is one
atomic transaction containing whole revisions, so interruption leaves either
the previous prune baseline or the fully advanced one.

## Reading Versus Controlling Revisions

DB Object reads are intentionally available for diagnostics, APIs and tooling:

- inspect current catalog state;
- list retained revisions;
- page through delta metadata;
- expose revision health and retention status.

Control remains on `revision::store` because it must enforce:

- head continuity;
- reverse application order;
- Blob retention barriers;
- journal phase and recursive-capture exclusion;
- transaction ownership;
- typed corruption handling.

Reading system rows and safely reverting them are not equivalent operations.

## Plugin Integration

The library design does not depend on a plugin. The DB Store plugin can configure
the optional revision layer for one named physical store:

```text
one named store
  one DB Core driver
  optional DB Object store
  optional DB Blob store
  optional DB Revision store
```

The runtime handle exposes `revisions()` alongside `objects()` and `blobs()` when
the layer is configured. It constructs all enabled layers over the same driver
and preserves one shared transaction boundary. Revision scope remains explicit:
ordinary plugin transactions do not create revisions automatically.

Plugin configuration and lifecycle do not alter the library contracts in this
document. Runtime retention policy remains product-owned. The implemented
runtime shape and remaining state-service boundaries are recorded in
[Forge DB State Services v1](forge-db-state-services-v1.md).

## Error Model

DB Revision requires typed exceptions for at least:

- invalid or incompatible configuration;
- unsupported persisted format;
- missing state/header row;
- corrupt entry or delta encoding;
- duplicate/missing delta ordinal;
- stale parent or stale head;
- requested revision is not the active head;
- requested revision was pruned;
- revision ID overflow;
- recursive capture attempt;
- prepare failure;
- Blob retention failure;
- transaction closed or rollback-only;
- backend conflict.

Native RocksDB statuses and object codec exceptions may be attached as diagnostic
context but must not leak as the only public error classification.

## Crash And Failure Semantics

- Application state and its revision journal commit in one backend transaction.
- Prepare failure leaves neither application changes nor revision rows.
- Process termination before backend commit exposes neither side.
- Process termination after backend commit exposes both sides.
- Revert is itself one atomic backend transaction.
- Failed revert leaves the original state and head unchanged.
- Failed prune must not release Blob retention before its journal boundary is
  durably removed.
- Corrupt revision data fails closed before restoration writes where possible.
- Dropped transaction cleanup writes no revision.

## Concurrency

Within one process, revision stores sharing driver ownership and Object family
must share runtime serialization for state/head mutation.

Across backend transactions:

- concurrent revision scopes may perform work independently;
- only one may commit against a particular head value;
- a stale scope fails rather than silently becoming a sibling branch;
- retry opens a fresh scope and receives the new head/ID;
- read-only Object snapshots may inspect committed revision tables concurrently.

Cross-process correctness requires backend conflict detection or locking on the
revision state row. A backend that cannot provide it must reject multi-process
revision writers or document a single-writer deployment requirement.

## Raw And Storage Format

- System objects use DB Object's existing raw-compatible object/index encoding.
- Reflection field order is explicit and stable.
- `format_version` gates all persisted revision rows.
- Family and key bytes are length-delimited.
- Delta ordinals are strict and contiguous from zero.
- Large before-images require configured limits before allocation.
- Unknown fields/versions fail closed unless a versioned migration explicitly
  supports them.
- Blob payload bytes are referenced/retained, not embedded repeatedly.

No storage format is declared stable until golden fixtures and reopen tests are
committed.

## Non-Goals

- No downstream product policy or vocabulary.
- No branch graph or multiple active heads.
- No reapply in v1.
- No arbitrary checkout of historical state.
- No automatic schema migration runner.
- No after-commit journal written in a second transaction.
- No public mutation access to system tables.
- No copying all Blob payloads into revision deltas.
- No raw RocksDB types in public APIs.
- No ID free list or reuse after committed revision removal.

## Implementation Blocks

### Block 1: DB Core Participation

- record-level before-image capture;
- mutation classification and exclusions;
- prepare-before-commit participants;
- savepoint frame integration;
- deterministic participant order;
- rollback-only handling after partial participant failure.

### Block 2: DB Object System Extensions

- central system object type catalog;
- narrow system-model registration;
- privileged transaction writes for system models;
- normal typed reads and indexes through Object store;
- compile-time rejection of application mutation APIs for system objects.

### Block 3: Revision Store

- open and validate catalog;
- assign candidate revision ID and parent;
- normalize/coalesce deltas;
- persist state, entry and delta rows atomically;
- implement head reads and continuity checks.

### Block 4: Revert And Prune

- reverse before-image application;
- head-only validation;
- bounded delta loading and corruption checks;
- journal deletion and monotonic ID preservation;
- bounded pruning and retained boundary checks.

### Block 5: DB Object And DB Blob Integration

- object/index capture policy;
- object sequence exclusion and no-ID-reuse tests;
- Blob owner-reference deltas;
- revision-owned Blob retention barriers;
- shared transaction tests across Object, Blob and direct Core records.

## Required Tests

### Persisted System Tables

- opening an empty Object store creates valid revision state;
- reopen reads identical state through Object API;
- state, entry and delta models are readable and indexable;
- public Object mutation APIs reject system models at compile time;
- system type conflicts and missing headers fail closed;
- package consumer can import revision types and read them through Object store.

### Atomic Capture

- object record and every secondary index share one revision;
- Blob owner changes share the same revision;
- direct Core records share the same revision;
- prepare failure commits neither state nor journal;
- observer runs only after data and journal commit;
- revision system writes do not recursively create deltas;
- repeated writes coalesce to the earliest before-image;
- net-zero changes emit no delta.

### Identity And Continuity

- first committed revision receives ID 1;
- committed IDs increase monotonically;
- outer rollback creates no entry;
- revert and prune never reuse committed IDs;
- stale-head concurrent commit fails;
- overflow fails before mutation;
- parent chain remains valid across commit, revert and prune boundaries.

### Savepoints

- rolled-back frame contributes no deltas;
- released frame merges correctly;
- nested writes preserve earliest before-image;
- outer rollback writes no revision;
- one outer commit writes one revision regardless of savepoint count.

### Revert

- insert reverts to absence;
- erase restores exact previous bytes;
- replace restores exact previous bytes;
- multiple keys restore in reverse ordinal order;
- Object indexes remain consistent;
- generated object IDs remain consumed;
- Blob ownership and retention remain valid;
- non-head, pruned and corrupt revisions fail without partial restoration;
- crash/reopen sees either complete old or complete restored state.

### Prune

- retained revisions remain revertible;
- pruned revisions cannot be reverted;
- head and application state do not change;
- Blob barriers release only after the required revision is removed;
- bounded multi-page deletion maintains valid progress/state;
- restart during a future checkpointed prune resumes deterministically.

### Backends

The complete semantic suite must pass against the deterministic DB Core test
driver and DB RocksDB. Backend-specific tests must prove conflict handling,
atomic reopen behavior and storage corruption diagnostics.

## Donor Evidence

### Spring Chainbase

Reviewed:

- `spring/libraries/chaindb/include/chainbase/chainbase.hpp`;
- `spring/libraries/chaindb/include/chainbase/undo_index.hpp`.

Accepted:

- database-wide undo boundary;
- monotonically tracked revision state;
- latest-revision undo;
- nested boundary squash;
- bounded removal of old undo history;
- generated ID state requires deliberate treatment.

Adapted:

- Chainbase `database` owns all indexes, while FORGE composes sibling DB layers
  over a Core driver;
- FORGE stores typed revision tables durably instead of relying on Chainbase's
  in-memory mapped undo stack shape;
- FORGE uses asynchronous backend transactions and participants.

Rejected:

- downstream controller vocabulary;
- direct placement on DB Object alone;
- assuming all physical records can be blindly restored;
- treating Chainbase `commit(revision)` as ordinary backend commit.

### RocksDB TransactionDB

Accepted:

- one atomic transaction may span Object, Blob and revision families;
- transaction conflict/lock semantics protect shared state updates;
- native savepoints implement partial rollback inside the active transaction.

Rejected:

- exposing RocksDB types publicly;
- relying on native savepoints for durable post-commit revision history;
- assuming the backend understands Object ID or Blob retention semantics.

## Acceptance Criteria

DB Revision may be called production-ready only when:

- revision tables are typed DB Object system models and readable through the
  configured Object store;
- Object, indexes, Blob references and direct Core records commit with one
  journal atomically;
- revision writes cannot capture themselves;
- savepoint rollback cannot leak discarded deltas;
- committed IDs remain monotonic and unreused;
- revert is head-only, atomic and byte-exact;
- generated Object IDs and Blob retention remain safe;
- pruning is bounded and cannot remove required state;
- deterministic and RocksDB suites pass with crash/reopen coverage;
- public APIs remain neutral and backend-independent.
