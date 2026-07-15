# Forge DB MDBX Driver v1

Status: DB MDBX driver implemented and verified. Typed configured ownership in
`plugins.db.store` is implemented by the focused follow-up
`forge-db-store-mdbx-v1`.

Donor evidence is recorded in
[DB MDBX Driver Donor Baseline v1](../donors/forge-db-mdbx-v1.md).

## Purpose

Add libmdbx as a second implementation of `forge::db::core::driver`, alongside
DB RocksDB. The driver is optimized for a bounded, ordered state store where
point/range read latency matters more than parallel write throughput.

The driver must immediately support the existing backend-neutral stack:

- DB Core transactions, snapshots, participants and savepoints;
- DB Object records, indexes, streams and ranked `count`/`sum` indexes;
- DB Revision capture, revert and bounded prune;
- programmatic use through DB Store plugin handles;
- independent package consumption.

Nothing above DB Core is reimplemented inside DB MDBX.

## Backend Selection

| Workload | MDBX | RocksDB |
| --- | --- | --- |
| Ordered single-writer consensus/state-machine state | Preferred candidate | Supported |
| Read-latency-critical point/range access | Preferred candidate | Supported with LSM read amplification |
| Write-heavy independent product records | Not the default | Preferred |
| Data set substantially larger than useful page cache | Requires measured geometry/profile | Preferred baseline |
| Blob payloads and Blob files | Out of scope | Preferred |
| Replayable state with tolerated lost tail | `safe_nosync` candidate | WAL/write-options policy |
| State without a replay source | Durable sync only | Durable RocksDB configuration |

This is a profile choice, not a universal backend replacement.

## Public Identity

```text
path       libraries/db/mdbx
target     forge_db_mdbx
component  db_mdbx
namespace  forge::db::mdbx
modules    forge.db.mdbx.driver
           forge.db.mdbx.exceptions
```

`forge_db_mdbx` publicly links `forge_db_core`, `forge_asio` and
`forge_exceptions`. Vendored MDBX remains a private dependency. No native MDBX
type appears in exported module declarations.

## Public API

```cpp
namespace forge::db::mdbx {

enum class durability {
   durable_sync,
   safe_nosync,
};

struct geometry {
   std::optional<std::uint64_t> lower_size;
   std::optional<std::uint64_t> current_size;
   std::optional<std::uint64_t> upper_size;
   std::optional<std::uint64_t> growth_step;
   std::optional<std::uint64_t> shrink_threshold;
   std::optional<std::uint32_t> page_size;
};

struct config {
   std::string path;
   std::vector<std::string> families{"default"};
   durability durability_mode = durability::durable_sync;
   geometry map;
   std::size_t max_readers = 128;
   bool create_if_missing = true;
   bool create_missing_families = true;
};

class driver final : public forge::db::core::driver {
 public:
   static boost::asio::awaitable<std::shared_ptr<driver>> open(
      config value,
      forge::asio::affine::executor executor);

   boost::asio::awaitable<void> async_flush(bool sync) override;
};

} // namespace forge::db::mdbx
```

The shape above intentionally differs from copying the DB RocksDB constructor:
MDBX environment and DBI initialization can perform filesystem I/O and native
write transactions, so an async factory is the honest boundary. The executor
is runtime ownership, not serializable config.

Configuration is described with Boost.Describe and Forge Schema. Validation
must happen before opening native resources:

- path and family names are non-empty;
- family names are unique;
- `max_readers` is non-zero and fits the MDBX API;
- geometry values fit native signed limits and obey lower/current/upper order;
- growth and shrink values are compatible with page size;
- page size is supported by MDBX;
- an explicit upper size is strongly recommended for production;
- existing environments reject incompatible page size/geometry;
- environment creation and missing-family creation are independent switches.

`MDBX_UTTERLY_NOSYNC` is not part of production public configuration. Native
tests that need it may use a test-only internal option.

## Library Layout

The MDBX driver must follow `create-library`; the monolithic anonymous classes
currently present in DB RocksDB are not copied.

```text
libraries/db/mdbx/
  CMakeLists.txt
  README.md
  include/forge/db/mdbx/
    driver.cppm
    exceptions.cppm
  details/
    driver_impl.hxx
    environment.hxx
    transaction_session.hxx
    snapshot_session.hxx
    error.hxx
    scan.hxx
  driver.cpp
  driver_impl.cpp
  environment.cpp
  transaction_session.cpp
  snapshot_session.cpp
  error.cpp
  scan.cpp
```

Ownership:

- `driver.cppm` and `driver.cpp` own the public driver facade;
- `driver_impl.hxx/.cpp` own `driver::impl` and coordinate environment,
  families and executor;
- `environment.hxx/.cpp` own native environment/DBI lifetime;
- each Core session implementation has its exact private pair;
- shared native-code translation lives in `error.hxx/.cpp` because it is used
  by environment and both session implementations;
- range/cursor conversion lives in `scan.hxx/.cpp` because both transaction and
  snapshot sessions use it.

No class is declared only inside a `.cpp`, no private header lives in the root,
and no decorative helper or empty source is added.

## Vendored Dependency

The official amalgamated release is vendored under `vendor/libmdbx/` with an
exact release manifest. Forge builds one private static C target from the
official amalgamation. MDBX tools and donor tests are not built.

The implementation commit must update:

- `PROVENANCE.md`;
- `THIRD_PARTY_LICENSES`;
- the install/export dependency graph;
- root CMake feature reporting;
- package component discovery.

MDBX is vendored, so `db_mdbx` does not depend on a machine-installed MDBX
package. A build option may disable the backend, but enabling it must not depend
on network access at configure time.

## Environment And Family Lifecycle

1. Validate config without native resources.
2. Create/open the environment with `MDBX_NOSTICKYTHREADS` and
   `MDBX_EXCLUSIVE`.
3. Do not enable `MDBX_WRITEMAP`.
4. Apply max readers, derived max DBIs, geometry and durability before open.
5. On the affine lane, open one initialization write transaction.
6. Open every configured family as a named DBI, using create flags only when
   allowed.
7. Commit family creation atomically and cache every DBI until environment
   close.
8. Publish the driver only after all families are ready.

V1 deliberately rejects cooperative multi-process access and a second open of
the same environment path. `MDBX_EXCLUSIVE` turns that non-goal into an
enforced runtime contract instead of a documentation wish.

Family lookup never opens DBIs lazily. An unknown Core family is a typed invalid
descriptor and causes no native mutation.

## Writer Execution And Admission

MDBX write locking is thread-affine. The driver therefore uses two distinct
mechanisms:

1. an async FIFO gate controls which Core transaction owns the environment's
   writer slot;
2. one externally owned affine executor runs every native operation for that
   write transaction on one operating-system thread.

The gate is acquired before posting `mdbx_txn_begin(..., MDBX_TXN_TRY, ...)`.
Calling native begin first is forbidden: MDBX could block the only affine lane
while operations needed to finish the current writer are queued behind it.

The gate ticket is owned by the transaction session and released only after:

- successful commit;
- explicit rollback;
- dropped Core transaction cleanup has completed native abort;
- failed begin has cleaned up all partial native state.

Every transaction operation is posted to the same affine executor and awaited
from the caller's original Asio executor. Cancellation may remove work before
native execution starts. Once a mutation starts, cancellation cannot report it
as not executed; the operation completes with its real result and Core decides
whether the transaction becomes rollback-only.

The existing DB Object gate cannot be imported as a private cross-library
detail. The implementation should extract its tested cancellation/FIFO ticket
mechanics into a neutral Forge Asio async gate rather than duplicate them.

## Savepoints

The session stores a stack of native write transactions:

```text
root write transaction
  -> nested savepoint transaction
     -> nested savepoint transaction
```

- create begins a child of the current top;
- rollback aborts and removes the top child;
- release commits the top child into its parent and removes it;
- only the top savepoint can be consumed, matching DB Core LIFO semantics;
- the parent is never used while a child is active;
- outer commit commits the current root after Core has closed logical frames;
- outer rollback aborts the top child chain and root on the affine thread.

`MDBX_WRITEMAP` remains disabled because it is incompatible with nested write
transactions.

## Snapshot Execution

One Core snapshot must represent one MDBX MVCC point while still supporting
concurrent reads through copied high-level views.

The snapshot session owns:

- one anchor read transaction that is never used for ordinary CRUD;
- a pool of active read clones created from that anchor;
- a short internal lock for clone checkout/creation/return only.

Each `get` or `scan_page` checks out exactly one clone. A cursor is local to the
operation and is closed before returning the clone. Separate operations may use
separate clones concurrently and still observe the anchor's exact snapshot.

The pool grows only to observed concurrent demand. `MDBX_READERS_FULL` is
translated to a typed error; unlimited hidden queueing is not introduced. The
anchor and all clones are aborted when the last Core snapshot copy is gone.

This design avoids both invalid concurrent use of one native transaction and a
mutex that serializes all high-level snapshot reads.

## CRUD And Scan Mapping

All values returned to DB Core are copied into `std::vector<std::byte>` before
the native transaction/cursor lease ends.

`get_for_update()` is equivalent to `get()` inside the sole native writer. The
session reports `record_locks = true` because the writer gate excludes every
other writer for the physical environment, including Object backend-policy
coordination.

Before native calls, the driver validates key length using the opened
environment's actual maximum. It validates family existence, key/range shape
and native size conversions. MDBX errors are never allowed to become assertions
or untyped integers at the public boundary.

`scan_page()` must preserve the complete DB Core contract:

- bytewise ordering;
- optional prefix;
- half-open `[begin, end)` bounds;
- an exclusive continuation cursor;
- presence of an empty-key cursor;
- no continuation when the next native key is outside the range;
- no Object materialization.

The implementation uses `SET_RANGE` for the first candidate and `NEXT` for
iteration, peeking at most one extra in-range entry to decide continuation.

## Durability And Flush

### `durable_sync`

MDBX writes and flushes data before publishing durable metadata. This is the
safe neutral Forge default for data without a replay source.

### `safe_nosync`

MDBX preserves a previous fully steady snapshot while recent transactions may
remain unsynced. A system crash may lose the tail but should reopen at a valid
steady commit. This is suitable only when the consumer has an explicit replay
source and has tested recovery.

`async_flush(true)` creates a new forced steady point with
`mdbx_env_sync_ex(force=true, nonblock=false)`. It acquires the writer gate and
runs on the affine executor so it neither races an active writer nor blocks an
Asio runtime thread.

`async_flush(false)` uses the non-blocking poll form. Return-code handling must
distinguish completed, no-work and still-busy outcomes without spinning.

The initial proposal used `safe_nosync` as the default because the blockchain
consumer is replayable. This design changes the neutral library default to
`durable_sync`; the blockchain must opt into tail-loss semantics explicitly.

## Geometry And Reader Pressure

Geometry is applied deliberately rather than relying on MDBX's very small
compatibility default. Production config should set a realistic upper bound and
growth step.

Typed diagnostics distinguish:

- configured upper bound reached;
- filesystem out of space;
- address-space/map resize failure;
- reader table exhaustion;
- write transaction capacity exhaustion;
- incompatible existing page size/geometry.

Long snapshots retain retired pages. The driver README must explain that a
snapshot is operation-scoped or bounded-batch state, not a daemon-lifetime
cache. Tests record reader lag and retained space through MDBX transaction/env
information. V1 exposes no product metrics API solely for MDBX; a future neutral
DB diagnostics surface can consume the same data.

## Typed Errors

`forge.db.mdbx.exceptions` owns backend-specific typed failures such as:

- `invalid_config`;
- `environment_busy`;
- `incompatible_environment`;
- `corruption`;
- `map_full`;
- `readers_full`;
- `transaction_full`;
- `key_too_large`;
- `value_too_large`;
- `io_error`.

Normal absence remains `std::nullopt`; deleting a missing key remains
idempotent. Unsupported Core operations use DB Core's existing
`unsupported_operation`. Error contexts include operation, path, family and
native code/message without exposing native types.

## Core, Object And Revision Compatibility

The MDBX driver contains no participant, Object, Revision or aggregate logic.
Compatibility is proven by running the same behavior fixtures against a driver
factory:

- Core transaction/savepoint/snapshot tests;
- Object primary/secondary/ranked index tests;
- generated-ID and writer-policy tests;
- Revision capture/revert/prune tests;
- shared Object/Blob read snapshot tests only for logical layers configured on
  MDBX-sized records, not large Blob payload policy;
- programmatic DB Store tests using a caller-created MDBX driver.

Configuration support for `driver: mdbx` in `plugins.db.store` is a follow-up
plugin change. V1 may pass a MDBX driver through the existing programmatic
`add_store(...)` API; the plugin must not instantiate private worker threads.

## Tests

### Contract Parity

- transaction commit, rollback and dropped cleanup;
- snapshot visibility and concurrent copied reads;
- all scan-page edge cases already covered for RocksDB;
- native nested savepoint interleavings;
- participant capture and rollback-only transitions;
- DB Object indexes, streams and ranked aggregates;
- DB Revision atomic capture, revert and bounded prune;
- package consumer for component `db_mdbx`.

### MDBX-Specific

- caller coroutine migrates across runtime workers while every native write
  operation remains on the same affine lane thread;
- N concurrent writers are admitted FIFO without blocking runtime workers;
- canceled waiters do not steal or wedge the writer ticket;
- dropped sessions abort before the next writer is admitted;
- concurrent Core snapshot reads use distinct clones and identical snapshot
  IDs;
- reader exhaustion and release are typed and leak-free;
- geometry grows by configured steps and fails with typed map-full at its cap;
- key-size validation follows configured page size;
- long readers delay reuse and release it after snapshot destruction;
- `safe_nosync` kill/reopen recovers a committed prefix without corruption;
- durable sync survives the same harness at the last acknowledged commit;
- environment open rejects duplicate path ownership;
- no MDBX call blocks an ordinary Asio runtime worker.

### Performance Evidence

Informational microbenchmarks compare MDBX and RocksDB over identical encoded
records:

- point get;
- lower-bound and bounded scan;
- short transaction commit;
- savepoint create/release/rollback;
- ranked count/sum/rank queries;
- concurrent snapshot reads.

The benchmark is not a correctness gate, but the MDBX backend must demonstrate
the read-latency reason for carrying a second backend before production
adoption.

## Delivered Order

1. Confirm the execution and durability decisions below.
2. Add the reusable affine execution and async gate primitives.
3. Vendor one official MDBX release with provenance and license evidence.
4. Add typed config/errors and async environment open.
5. Add snapshot anchor/clone pool and scan parity.
6. Add FIFO writer admission, affine transaction session and CRUD.
7. Add nested native savepoints.
8. Add forced/non-blocking sync and deterministic close ordering.
9. Parameterize Core/Object/Revision fixtures and add MDBX-specific tests.
10. Add package export, README and optional programmatic DB Store coverage.

## Non-Goals

- DB Revision implementation or revision IDs;
- DB Object ranked aggregate implementation;
- zero-copy public reads;
- cooperative multi-process environment access;
- dynamic family creation after driver publication;
- custom MDBX comparators;
- `MDBX_WRITEMAP`;
- Blob-file replacement or large Blob payload policy;
- automatic plugin config support for `driver: mdbx`;
- replacing DB RocksDB;
- backend-specific concepts in DB Core public records.

## Confirmed Implementation Decisions

### 1. Thread-Affine Execution

Recommendation: add a narrow reusable `forge.asio.affine` lane with one joined
native worker, bounded FIFO admission, cancellation-before-start and
deterministic drain. DB MDBX receives only its executor; it does not create a
private thread pool.

Using `forge.asio.compute` is rejected because that module is explicitly a CPU
pool and does not promise a one-thread affinity contract. Using an Asio strand
is rejected because a strand does not pin an OS thread.

### 2. Async Gate Ownership

Recommendation: extract the proven DB Object `write_gate` mechanics into a
neutral Forge Asio async gate and reuse it from DB Object and DB MDBX. Copying
the implementation into MDBX would create two cancellation/fairness contracts.

### 3. Durability Default

Recommendation: neutral Forge defaults to `durable_sync`. Replayable blockchain
state explicitly selects `safe_nosync` and owns replay/recovery tests.

### 4. Environment Close

Recommendation: the affine lane owner outlives every MDBX driver/session. The
driver schedules final sync/close on that lane, and owner shutdown drains it.
If this cannot be expressed without relying on destructor timing, add a neutral
async close hook to DB Core rather than blocking a runtime worker in a
destructor.

### 5. Plugin Scope

The driver PR intentionally proved direct library and programmatic
`add_store(...)` use first. The focused `forge-db-store-mdbx-v1` follow-up adds
typed MDBX configuration and one managed affine lane per named configured
store. Programmatic drivers remain caller-owned.

### 6. Source Pin

Recommendation: choose the latest stable official 0.14.x release that passes
the Forge toolchain matrix, verify its official archive checksum independently,
and record it before source import. Do not pin a rolling generated docs build or
an unverified GitHub mirror snapshot.
