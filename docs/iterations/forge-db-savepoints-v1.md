# Forge DB Savepoints v1

Status: accepted design direction, implementation pending. This document fixes
the required semantics and correctness boundaries. Public API spelling may still
be refined during implementation, but an implementation must not weaken the
invariants below.

## Purpose

A savepoint is a named boundary inside one active database transaction. It lets
the caller undo a suffix of the transaction without discarding the transaction
itself.

FORGE needs this primitive at `forge.db.core` level because one core transaction
may be shared by several higher-level libraries. Rolling back to a savepoint must
therefore restore all joined layers together, including:

- direct DB Core records;
- DB Object records, secondary indexes and pending observer changes;
- DB Blob data and owner-reference records;
- a future DB Revision mutation accumulator;
- any later transaction participant that owns state outside the backend write
  batch.

A savepoint is not a second transaction and does not commit anything. It remains
inside the same backend session, lock ownership and outer commit/rollback
boundary.

## Decision Summary

- Savepoints extend `forge::db::core::transaction`.
- The identifier is a scalar alias, not a wrapper type:

  ```cpp
  using savepoint_id_t = std::uint64_t;
  ```

- Savepoints are nested and closed in last-in, first-out order.
- Both rollback and release consume the selected top savepoint.
- `rollback_to_savepoint()` removes changes made after the point and keeps the
  outer transaction active.
- `release_savepoint()` removes the boundary while keeping its changes.
- Full `transaction::rollback()` still removes all transaction changes.
- `transaction::commit()` persists all remaining changes and closes all open
  savepoint boundaries.
- Savepoint state is volatile and never survives transaction close or process
  restart.
- RocksDB uses its native transaction savepoint operations.
- Higher-level transaction participants must checkpoint and restore their own
  in-memory state in lockstep with the backend.
- Generated DB Object identifiers consumed after a savepoint are not reused
  after savepoint rollback.
- A savepoint does not create a durable DB revision.

## Terminology

### Outer Transaction

The `forge::db::core::transaction` returned by
`forge::db::core::driver::begin_transaction()`. It owns the backend transaction
session and the final commit or rollback decision.

### Savepoint

A temporary boundary inside the outer transaction. It records enough backend
and participant state to undo only the changes made after that boundary.

### Release

Remove the latest savepoint while keeping all changes made after it. If a parent
savepoint exists, the released changes become part of that parent's scope.

### Rollback To Savepoint

Undo changes made after the latest savepoint, consume that savepoint and keep the
outer transaction active.

### Participant

A higher-level transaction component that uses the core transaction but also
keeps non-backend state which must follow savepoint transitions. DB Object is a
participant because it tracks pending object changes, observer input and object
ID allocation state outside the backend write batch.

## Current Gap

The current `forge::db::core::transaction` supports:

- record `get`, `put`, `erase` and paged scans;
- final `commit()` and `rollback()`;
- asynchronous `after_commit` and `after_rollback` hooks.

Those final hooks are insufficient for savepoints:

- they only run when the outer transaction closes;
- they cannot restore an Object transaction's intermediate `change_set`;
- they cannot remove observer-visible mutations that were rolled back to a
  savepoint;
- they cannot preserve a consumed object ID high-water mark while undoing the
  corresponding object insert;
- they cannot keep a future revision delta accumulator aligned with the backend
  write batch.

Savepoint support therefore requires a nested transaction-participant mechanism,
not only three new public methods forwarded to RocksDB.

## Candidate Public API

```cpp
export namespace forge::db::core {

using savepoint_id_t = std::uint64_t;

class transaction {
public:
   boost::asio::awaitable<savepoint_id_t> create_savepoint();

   boost::asio::awaitable<void>
   rollback_to_savepoint(savepoint_id_t savepoint);

   boost::asio::awaitable<void>
   release_savepoint(savepoint_id_t savepoint);
};

} // namespace forge::db::core
```

The methods belong to the existing concrete move-only transaction. No separate
public savepoint transaction class is required.

The backend capability surface must report whether savepoints are supported. An
unsupported backend must fail with a typed DB Core exception before mutating
transaction state; it must not silently emulate partial behavior.

## Lifetime And Identity

A `savepoint_id_t` has transaction-local meaning:

- it is returned only by `create_savepoint()`;
- it is valid only while the owning transaction is active;
- it is consumed by successful rollback or release;
- it becomes invalid when the outer transaction commits or rolls back;
- it is not serialized, persisted or compared across transactions;
- it is not a revision ID and must not be stored as one.

The implementation may allocate monotonically increasing IDs inside each core
transaction. Wraparound must be detected before changing backend or participant
state and reported as a typed failure or `std::overflow_error`, consistent with
the final API policy.

Passing a stale or non-top ID is a programming error represented by a typed DB
Core exception. Because the public ID is intentionally a scalar, callers must
not mix IDs from different transactions.

## Stack Semantics

Savepoints form a stack.

```text
transaction begins
  create A       stack [A]
  write x
  create B       stack [A, B]
  write y
```

Only `B` may be released or rolled back at this point.

### Roll Back B

```text
rollback_to_savepoint(B)
  undo y
  consume B
  stack [A]
```

The write to `x` remains pending in the outer transaction.

### Release B

```text
release_savepoint(B)
  keep y
  consume B
  stack [A]
```

Both `x` and `y` are now inside `A`'s scope.

### Out-Of-Order Close

Calling `rollback_to_savepoint(A)` or `release_savepoint(A)` while `B` is still
open is rejected. The implementation must not implicitly close unknown nested
scopes because every participant must observe the same deterministic nesting.

## Outer Commit And Rollback

An outer commit persists every change that has not been rolled back, regardless
of whether savepoint boundaries are still open. Successful commit closes all
savepoints and participant frames.

An outer rollback discards every pending change and closes all savepoints.

This matches the ownership rule that only the outer transaction controls durable
visibility. Requiring callers to release every savepoint before commit would add
ceremony without adding a stronger atomicity guarantee.

After commit or full rollback, all previous `savepoint_id_t` values are invalid.

## Pure DB Core Example

```cpp
std::shared_ptr<forge::db::core::driver> driver = make_driver();

auto tx = co_await driver->begin_transaction();

co_await tx.put(records, key_a, value_a);

auto point = co_await tx.create_savepoint();
co_await tx.put(records, key_b, value_b);

co_await tx.rollback_to_savepoint(point);
co_await tx.commit();
```

The committed state contains `key_a` but not `key_b`.

## Nested Example

```cpp
auto tx = co_await driver->begin_transaction();

auto outer = co_await tx.create_savepoint();
co_await tx.put(records, key_a, value_a);

auto inner = co_await tx.create_savepoint();
co_await tx.put(records, key_b, value_b);

co_await tx.release_savepoint(inner);
co_await tx.rollback_to_savepoint(outer);
co_await tx.commit();
```

Releasing `inner` preserves `key_b` only inside the transaction. Rolling back
`outer` then removes both `key_a` and `key_b`. The final commit is valid and may
persist unrelated changes made before `outer`.

## Shared DB Object And DB Blob Example

This is a library-layer example. It does not depend on the DB Store plugin.

```cpp
std::shared_ptr<forge::db::core::driver> driver = make_driver();

forge::db::object::store objects{
   driver,
   {.family = forge::db::core::family{"objects"}},
};

forge::db::blob::store blobs{
   driver,
   {
      .data_family = forge::db::core::family{"blobs.data"},
      .refs_family = forge::db::core::family{"blobs.refs"},
   },
};

auto tx = co_await driver->begin_transaction();
auto object_tx = co_await objects.join(tx);
auto blob_tx = blobs.join(tx);

auto point = co_await tx.create_savepoint();

auto content = co_await blob_tx.put(payload);
co_await object_tx.insert(file_metadata{.content = content});

co_await tx.rollback_to_savepoint(point);
co_await tx.commit();
```

The blob record, blob reference changes, object record, indexes and pending
Object observer change are all removed together. The outer transaction remains
usable after the savepoint rollback.

## Backend Contract

DB Core must extend its backend-neutral session contract. Exact names remain an
implementation decision, but the semantic operations are:

```cpp
class session {
public:
   virtual boost::asio::awaitable<void> create_savepoint() = 0;
   virtual boost::asio::awaitable<void> rollback_to_savepoint() = 0;
   virtual boost::asio::awaitable<void> release_savepoint() = 0;
};
```

The backend does not need the public scalar ID when it only supports a native
stack. DB Core validates the public top ID and then invokes the top-of-stack
backend operation.

The session capability set gains an explicit savepoint flag. This allows:

- feature detection before the first savepoint;
- deterministic package tests for supported and unsupported drivers;
- typed failure instead of backend-specific error leakage;
- later backends to opt in only after satisfying the complete contract.

## RocksDB Mapping

RocksDB TransactionDB already exposes the required physical operations:

| FORGE operation | RocksDB transaction operation | Effect |
| --- | --- | --- |
| `create_savepoint()` | `SetSavePoint()` | Capture current write batch, lock and transaction state. |
| `rollback_to_savepoint(id)` | `RollbackToSavePoint()` | Undo operations after the newest point and remove it. |
| `release_savepoint(id)` | `PopSavePoint()` | Remove the newest point and keep its operations. |

RocksDB also restores transaction-owned lock tracking and snapshot state as part
of its native implementation. FORGE must still synchronize all higher-level
participant state because RocksDB cannot see Object change sets, observers,
generated IDs or revision accumulators.

Raw `rocksdb::*` types and statuses must remain inside `forge_db_rocksdb`.

## Transaction Participant Model

The core transaction owns an ordered list of participants. A participant needs
savepoint-aware transitions conceptually equivalent to:

```text
checkpoint(savepoint_id)
rollback_to(savepoint_id)
release(savepoint_id)
commit()
rollback()
```

This is infrastructure for joined DB libraries, not a product callback API.
The final C++ shape must prevent a participant from outliving the core
transaction and must preserve the current move-only transaction ownership.

Each participant keeps a frame stack aligned one-to-one with the core savepoint
stack. Core validates stack identity before invoking backend or participant
operations.

### Create Ordering

Creating a savepoint must be all-or-nothing across backend and participants:

1. prepare participant checkpoint frames without publishing them;
2. create the backend savepoint;
3. publish all prepared participant frames and the public savepoint ID.

If preparation fails, no backend point is created. If backend creation fails,
the prepared frames are discarded. A savepoint ID is returned only after every
layer accepted the boundary.

### Rollback Ordering

Rollback changes physical state first, then restores participant state:

1. validate that the requested ID is the top savepoint;
2. ask the backend to roll back and consume its top point;
3. restore and consume each participant frame;
4. remove the core stack entry.

Participant restoration should be designed as deterministic and non-throwing
where possible. If backend rollback succeeds but participant restoration fails,
the transaction is no longer safe for commit. Core must mark it rollback-only;
the caller may only perform a full rollback. It must not continue with partially
restored high-level state.

### Release Ordering

Release validates the top ID, removes the backend point and merges/discards the
matching participant frames. If release cannot complete consistently, the
transaction becomes rollback-only.

### Final Hooks

Existing `after_commit` and `after_rollback` hooks retain their final-transaction
meaning. They do not run for savepoint release or savepoint rollback.

## DB Object Integration

DB Object keeps state outside the backend write batch and therefore must join as
a savepoint participant.

Each Object savepoint frame must cover at least:

- the pending `change_set` boundary;
- pending observer-visible mutations;
- interceptor-derived state, if any is retained by the transaction;
- generated object ID allocation high-water marks;
- any future write-policy state scoped to the transaction.

Rolling back to a savepoint removes changes appended after that frame. Releasing
a nested frame merges its surviving changes into the parent frame.

### Generated Object IDs

Generated IDs remain monotonic and are never reused. This rule also applies to a
savepoint rollback.

Example:

```text
create savepoint
create object with generated ID 41
rollback to savepoint
create another object
```

The next object receives ID 42, not 41.

The backend savepoint would normally restore the sequence record together with
the object insert. DB Object must therefore preserve the consumed allocation
high-water mark and write it back into the still-active outer transaction. This
is analogous to the existing full-rollback allocation seal, but it occurs inside
the same outer transaction instead of a separate post-rollback transaction.

If preserving that high-water mark fails, the transaction becomes rollback-only
and cannot commit an ID state that permits reuse.

## DB Blob Integration

DB Blob data and owner-reference writes already use the shared core transaction,
so native backend rollback removes their physical records together.

Any Blob in-memory bookkeeping added later must participate explicitly. The
savepoint contract must preserve these existing invariants:

- a stale wrong-size `ref` cannot mutate retention state;
- `retain`, `release`, `erase` and explicit collection remain transactional;
- rolling back a savepoint cannot leave an owner record without its blob;
- rolling back a savepoint cannot make a retained blob collectible;
- no automatic garbage-collection policy runs as part of savepoint handling.

## DB Revision Integration

Savepoints and durable revisions are related but separate mechanisms.

A future revision scope will accumulate record deltas while an outer transaction
is active. That accumulator must participate in savepoints:

- creating a savepoint opens a nested delta frame;
- rolling back the savepoint discards deltas produced after it;
- releasing the savepoint merges surviving deltas into the parent frame;
- only the successful outer commit may persist a durable revision record.

Savepoint creation, release and rollback never create standalone revisions.
One outer transaction with several savepoints still produces at most one durable
revision for an attached revision scope.

This relationship is why savepoint-aware participant infrastructure should land
before the revision library, even though the two public mechanisms have different
purposes and lifetimes.

See [Forge DB Revisions And Migrations](forge-db-revisions-migrations-v1.md) for
the separate durable history design.

## Reads, Scans And Cursors

Reads inside a transaction observe its current pending writes. After rollback to
a savepoint, subsequent reads must observe the restored transaction state.

Materialized values already returned to the caller are ordinary copies and are
not retroactively changed.

Transaction scans are not stable across writes or savepoint rollback. A caller
must restart a paged transaction scan after changing transaction state. A future
cursor epoch may make stale-cursor rejection explicit, but v1 must not promise
snapshot semantics for transaction cursors.

Stable iteration remains the responsibility of `forge::db::core::snapshot` and
is independent of transaction savepoints.

## Concurrency And Ownership

- Savepoint methods operate on one active transaction and are not concurrently
  callable on that transaction.
- Parallel independent transactions have independent savepoint stacks.
- A joined DB Object/Blob facade does not own commit, rollback or savepoint
  lifetime.
- The outer core transaction remains the only durable boundary.
- DB Object's single-writer gate remains held by its owning/joined transaction
  until the outer transaction closes; savepoint release does not release it.
- Savepoints do not weaken backend conflict detection or lock ownership.

## Cancellation And Failure

Savepoint transitions are state-machine operations. Cancellation or backend
failure must not expose an ambiguous active state.

Required behavior:

- failure before backend mutation leaves the transaction and stack unchanged;
- unsupported capability fails before participant state changes;
- backend failure with no confirmed state change leaves the point open;
- backend success followed by participant failure marks the transaction
  rollback-only;
- a rollback-only transaction rejects reads and writes except full rollback if
  continuing could expose inconsistent participant state;
- full rollback remains awaited and error-propagating;
- dropped-transaction cleanup remains best-effort/no-throw and must discard all
  savepoint frames together with the session.

The implementation must use typed DB Core exceptions for closed transaction,
unsupported operation, unknown/stale savepoint and rollback-only state. Native
backend status strings are diagnostic context, not public error categories.

## Persistence And Recovery

Savepoints are intentionally not durable metadata:

- they are not restored after restart;
- they do not appear in a system family;
- they do not need pruning;
- they cannot be reverted after outer commit;
- a process crash relies on backend transaction recovery to discard the whole
  uncommitted transaction.

Durable post-commit reversal belongs to DB Revision, not to savepoints.

## Non-Goals

- No nested independently committable transactions.
- No savepoint persistence across process restart.
- No cross-transaction savepoint IDs.
- No automatic revision creation.
- No schema migration policy.
- No automatic Blob garbage collection.
- No raw RocksDB types in public APIs.
- No plugin-specific savepoint API in v1; the library transaction is the owner.
- No backend emulation that only restores physical records while ignoring
  participant state.

## Implementation Blocks

### Block 1: DB Core State Machine

- add the scalar ID alias and public transaction methods;
- add capability reporting and typed errors;
- maintain the validated savepoint stack;
- add rollback-only state;
- close all frames on final commit/rollback/drop cleanup.

### Block 2: Backend Sessions

- extend the backend-neutral session contract;
- implement deterministic in-memory/test-driver savepoints;
- map DB RocksDB to `SetSavePoint`, `RollbackToSavePoint` and `PopSavePoint`;
- prove lock and pending-write behavior through backend tests.

### Block 3: Participant Infrastructure

- define internal participant registration and lifetime;
- implement prepare/publish/discard checkpoint frames;
- define failure ordering and rollback-only transitions;
- preserve existing final commit/rollback hooks.

### Block 4: DB Object And DB Blob

- checkpoint Object change sets and observer input;
- preserve generated ID high-water marks on partial rollback;
- prove object/index consistency;
- prove Blob data/reference atomicity;
- verify joined and owning transaction paths.

### Block 5: Revision Readiness

- expose the same internal participant boundary to DB Revision;
- test nested delta-frame merge/discard behavior;
- ensure revision journal families are excluded from recursive capture;
- keep durable revision persistence owned by outer commit.

## Required Tests

The feature is not production-ready until the same semantic suite passes against
the deterministic test driver and DB RocksDB where applicable.

### DB Core

- create, rollback and continue using one transaction;
- create, release and commit;
- nested release then outer rollback;
- nested rollback then outer commit;
- reject stale, unknown and non-top IDs;
- commit with open savepoints closes all frames;
- full rollback with open savepoints discards all changes;
- dropped transaction with open savepoints runs normal cleanup;
- unsupported backend fails before state mutation;
- backend rollback/release errors leave a deterministic state;
- participant failure marks the transaction rollback-only;
- transaction reads reflect restored state;
- transaction scan restart behavior is documented and tested.

### DB Object

- object insert/modify/erase rollback restores primary records;
- every secondary index matches restored object state;
- unique-index failure inside a savepoint does not corrupt the outer transaction;
- observer receives only changes surviving the outer commit;
- observer receives nothing for savepoint-rolled-back mutations;
- interceptor order remains deterministic;
- generated IDs are not reused after savepoint rollback;
- ID high-water state survives outer commit and store reopen;
- owning and joined Object transactions have identical behavior;
- Object single-writer gate remains held across savepoint operations.

### DB Blob

- put/retain rollback removes both data and owner changes;
- release rollback preserves the owner reference;
- erase rollback preserves the blob;
- explicit collect changes rollback atomically;
- wrong-size references remain rejected across savepoint transitions;
- Object metadata and Blob payload roll back together in a shared transaction.

### DB RocksDB

- native pending writes are restored to each nested point;
- release preserves writes;
- rollback restores transaction lock bookkeeping;
- no physical writes become visible before outer commit;
- process-level reopen observes only the final committed state;
- native errors map to typed DB Core exceptions.

### Future DB Revision

- rolled-back savepoint deltas never enter the durable revision;
- released nested deltas merge with the correct earliest before-image;
- repeated mutation of one key across nested frames produces one reversible final
  delta;
- savepoints do not increment durable revision identity;
- outer rollback writes no durable revision.

## Donor Evidence

The design is grounded in two distinct donor mechanisms:

### RocksDB TransactionDB

Reviewed file:

- `donors/rocksdb/include/rocksdb/utilities/transaction.h`

Accepted patterns:

- native nested savepoint stack;
- rollback of operations after the newest point;
- rollback consumes the newest point;
- pop/release consumes the newest point while keeping writes;
- backend transaction remains the final commit boundary.

Rejected as insufficient alone:

- treating native write-batch restoration as complete FORGE behavior without
  restoring higher-level participants.

### Spring Chainbase Undo Sessions

Reviewed file:

- `spring/libraries/chaindb/include/chainbase/undo_index.hpp`

Accepted patterns:

- nested undo boundaries;
- `undo()` consumes the latest boundary and restores its changes;
- `squash()` merges the latest boundary into its parent;
- deterministic last-in, first-out ownership.

Not copied into savepoints:

- durable revision numbering and retained undo history;
- Chainbase-specific object/index memory management;
- controller policy.

Those retained-history mechanics belong to the separate DB Revision design.

## Acceptance Criteria

Savepoints may be declared shipped only when:

- all joined layers observe one atomic partial rollback;
- in-memory and RocksDB semantics match;
- no generated object ID can be reused because of savepoint rollback;
- participant failures cannot leave a committable inconsistent transaction;
- final commit/rollback and dropped cleanup remain deterministic;
- public API contains no RocksDB or downstream product vocabulary;
- the required Core/Object/Blob/RocksDB package and behavior tests pass.
