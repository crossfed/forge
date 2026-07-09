# Forge DB Stabilization Donor Baseline v1

This note anchors the follow-up stabilization pass for the Forge DB family after
the shared driver, object store, blob store and DB Store plugin landed. The goal
is not to add new public API. The goal is to turn the transaction and lifecycle
rules into explicit invariants with donor-backed tests.

## Scope

- `forge.db.core`: transaction/session lifetime, commit failure, rollback
  failure, dropped cleanup, rollback hook ordering, cancellation and snapshot
  ownership.
- `forge.db.object`: monotonic ID allocation, allocation seals, shared runtime
  gates, owned vs joined transaction ownership, observer/interceptor ordering.
- `forge.db.blob`: typed `ref<Digest>` validation, retain/release/erase
  consistency, collection safety and raw codec hardening.
- `forge.db.rocksdb`: generic range mapping, cursor presence, snapshot parity
  and family-level BlobDB options.
- `forge.plugins.db.store`: shared transaction wrapper, object/blob coupling,
  configured family validation and lifecycle races.

## Donor Patterns

### RocksDB

RocksDB `TransactionDB` keeps write state in the transaction object. Forge must
treat the low-level DB session as the owner of backend write state until commit
or rollback completes and the session is destroyed. Cleanup hooks that open new
write work must run only after the dropped or explicit transaction session is
closed.

Accepted pattern:
- transaction lifetime is the backend write boundary;
- snapshots are stable read views;
- column families are logical keyspaces inside one physical DB;
- BlobDB options are family-level storage mechanics, not BlobDB domain policy.

Rejected pattern:
- opening nested write transactions while a dropped write session still owns
  backend write state.

### SQLite

SQLite has a single-writer transaction model with deterministic end-of-
transaction cleanup. Forge object writes use the same practical invariant:
writer gates must cover the full mutation lifecycle, including rollback cleanup
and allocation seals.

Accepted pattern:
- one writer lane by default;
- rollback is part of the transaction boundary, not an optional side effect;
- cleanup ordering must be deterministic even when errors are swallowed in a
  destructor path.

Rejected pattern:
- releasing a writer lane before follow-up cleanup that protects visible state
  invariants.

### PostgreSQL

PostgreSQL sequences are monotonic and gap-tolerant. Values are not reused after
rollback. Forge object ID allocation follows the same semantic shape: generated
IDs may have gaps, and gaps are preferable to stale-reference aliasing.

Accepted pattern:
- generated object IDs are monotonic;
- rollback, failed insert and delete do not return IDs to a free list;
- sequence high-water marks must survive store reopen inside the supported
  process/driver scope.

Rejected pattern:
- gap scanning or free-list reuse in the object store.

### LMDB

LMDB makes transaction lifetime the visibility boundary: readers see stable
views and writers commit atomically. Forge should preserve that shape across
`forge.db.core`, `forge.db.object` and `forge.db.blob` joins.

Accepted pattern:
- a joined high-level object/blob transaction uses the passed core transaction;
- joined facades do not own commit, but their hooks must remain alive until the
  core transaction commits or rolls back;
- read snapshots must not observe moving state across pages.

Rejected pattern:
- a high-level joined facade whose destruction silently drops commit/rollback
  hooks required for correctness.

### Chainbase

Chainbase undo sessions are deterministic state-transition boundaries. Forge
does not copy Chainbase, but it adopts the cleanup discipline: if a high-level
layer has derived records, index entries or allocation seals, rollback cleanup
must restore the engine invariants before the next writer is allowed to proceed.

Accepted pattern:
- derived index/ref state is maintained through the same mutation pipeline;
- observers run only after successful commit;
- rollback cleanup must not notify observers.

Rejected pattern:
- best-effort cleanup that can race with the next writer and expose stale
  derived state.

## Stabilization Checklist

- Prove `forge.db.core::transaction` commit failure preserves rollback state.
- Prove explicit rollback and dropped rollback close the backend session before
  rollback hooks open new write work.
- Prove rollback hooks run after backend rollback failure on the dropped path.
- Prove object ID allocation cannot reuse an ID after rollback plus store reopen
  over the same driver and object family.
- Prove joined object transactions keep allocation and observer hooks alive
  until the owning core transaction ends.
- Prove Blob retain, release, erase, has, get, stat and verify all handle stale
  `ref<Digest>` size consistently.
- Prove Blob collection only removes records discovered from stored data keys,
  not arbitrary stale refs.
- Prove RocksDB range scans and cursors preserve the generic DB half-open range
  contract, including empty keys.
- Prove DB Store plugin shared transactions hold the object writer gate when an
  object layer is configured.
- Prove configured object/blob families are distinct and lifecycle snapshots are
  copied under the plugin mutex.

## Non-Goals

- No new public API.
- No compatibility aliases for feature-branch names.
- No storage migration framework.
- No background Blob garbage collector.
- No product-specific DB policy.
- No plugin support for dynamic external driver lookup.
