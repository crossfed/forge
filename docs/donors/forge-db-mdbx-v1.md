# DB MDBX Driver Donor Baseline v1

Status: donor research recorded; implementation pending.

This note records the evidence used to design a second
`forge::db::core::driver` backend over libmdbx. It separates verified libmdbx
contracts from ecosystem precedent and from Forge-specific design decisions.

The corresponding Forge design is
[Forge DB MDBX Driver v1](../iterations/forge-db-mdbx-v1.md).

## Goal

MDBX is intended for stores with this profile:

- one ordered writer;
- many point and range readers;
- read latency is more important than write throughput;
- the working set is expected to fit a controlled memory and file geometry;
- a durable upstream log or another replay source can reconstruct a lost tail;
- DB Object, ranked indexes and DB Revision must remain backend-neutral.

This does not make MDBX the default Forge backend. RocksDB remains the general
backend for write-heavy product state, Blob files and workloads whose data set
or write pattern benefits from an LSM tree.

## Sources Reviewed

The upstream API documentation is rolling documentation. It was reviewed on
2026-07-15 against the published 0.14.2.x documentation. The implementation PR
must replace this moving reference with one exact release archive, version and
SHA-256 before any source is vendored.

### Official libmdbx

- [project and history](https://libmdbx.dqdkfa.ru/);
- [building and embedding](https://libmdbx.dqdkfa.ru/usage.html);
- [environment flags and opening](https://libmdbx.dqdkfa.ru/group__c__opening.html);
- [transactions and transaction cloning](https://libmdbx.dqdkfa.ru/group__c__transactions.html);
- [named tables and DBI handles](https://libmdbx.dqdkfa.ru/group__c__dbi.html);
- [geometry and limits](https://libmdbx.dqdkfa.ru/group__c__settings.html);
- [sync modes](https://libmdbx.dqdkfa.ru/group__sync__modes.html);
- [flush operations](https://libmdbx.dqdkfa.ru/group__c__extra.html);
- [errors and slow-reader handling](https://libmdbx.dqdkfa.ru/group__c__err.html).

Verified upstream properties:

- only one write transaction is active per environment;
- read transactions provide MVCC snapshots and do not block the writer;
- named tables are opened as DBI handles and require `max_dbs` before opening
  the environment;
- nested write transactions are supported to arbitrary depth and the parent is
  unavailable while a child is active;
- `mdbx_txn_clone()` creates a read transaction over the same MVCC snapshot;
- automatic geometry manages lower/current/upper size, growth step, shrink
  threshold and page size;
- long-lived readers delay reuse of retired pages;
- key size depends on database page size and is approximately half a page;
- `MDBX_SAFE_NOSYNC` preserves the last fully steady snapshot, so an operating
  system crash can lose recent transactions without corrupting the database;
- `MDBX_UTTERLY_NOSYNC` may corrupt the database after a crash and is not a
  production mode;
- `MDBX_WRITEMAP` is incompatible with nested transactions and therefore is
  incompatible with the required Forge savepoint mapping.

### Blockchain Engine Precedent

The libmdbx project records production use by Erigon, Akula, Silkworm and Reth.
That is evidence that an mmap B+tree is a credible state-store profile for
Ethereum clients. No source from those projects was copied or treated as an
implementation donor in this design pass.

Before claiming parity with one of those engines, a future donor update must
record an exact repository commit and the files actually reviewed. The current
Forge implementation remains an independent adapter to the neutral DB Core
contract.

## Critical Corrections To The Initial Work Plan

### Vendoring Uses The Official Amalgamation

Since December 2025, upstream distributes libmdbx as an amalgamated source
package. Forge must vendor an unmodified official release amalgamation, not a
partial reconstruction of the old source tree.

The vendored directory must include:

- the official C source and public headers needed to build the library;
- `LICENSE`, `NOTICE` and upstream copyright material;
- a Forge manifest containing the exact release, origin URL and SHA-256;
- matching entries in `PROVENANCE.md` and `THIRD_PARTY_LICENSES`.

Forge does not vendor MDBX command-line tools or its upstream test suite.

### `MDBX_NOSTICKYTHREADS` Is Necessary But Not Sufficient

`MDBX_NOSTICKYTHREADS` removes most transaction-to-thread checks and permits
read transaction use from different threads when use is serialized. It does
not make a write transaction freely migratable: a write transaction must be
committed or aborted on the same operating-system thread where it began.

Therefore Forge must not begin a native write transaction on an arbitrary Asio
runtime worker and later commit it from whichever worker resumes the coroutine.
The whole native write-session lifetime must execute on one thread-affine lane.

The lane is also the correct boundary for forced environment sync. A strand is
not sufficient because a strand serializes handlers but does not pin them to
one operating-system thread.

### Parallel Core Snapshot Reads Need Native Clones

Core snapshot handles are copyable and their read operations may overlap. One
MDBX transaction or cursor cannot be used concurrently from multiple threads,
including with `MDBX_NOSTICKYTHREADS`.

An MDBX snapshot session therefore needs one immutable anchor read transaction
and checked-out read clones created with `mdbx_txn_clone()`. Each in-flight
operation owns one clone and one cursor. Clones read the same MVCC snapshot and
must never be shared concurrently.

### `MDBX_WRITEMAP` Is Rejected

Forge savepoints map to nested native write transactions. Upstream explicitly
marks `MDBX_WRITEMAP` incompatible with nested transactions. The v1 driver must
not expose or enable this flag.

## Accepted Donor Patterns

| Requirement | Accepted MDBX mechanism |
| --- | --- |
| Core family | Named DBI opened once and retained for the environment lifetime |
| Core transaction | One native read-write transaction held on the affine writer lane |
| Core snapshot | Anchor read transaction plus same-snapshot clones for concurrent operations |
| `get` | `mdbx_get()` followed by a copy into Forge-owned bytes |
| `put` | `mdbx_put(..., MDBX_UPSERT)` after key/value validation |
| `erase` | `mdbx_del()` with not-found treated as an idempotent absence |
| `get_for_update` | Normal `get` inside the sole native writer |
| Ordered scan | Cursor `SET_RANGE`/`NEXT` with Forge half-open range validation |
| Savepoint create | Begin nested write transaction with current transaction as parent |
| Savepoint rollback | Abort the top nested transaction |
| Savepoint release | Commit the top nested transaction into its parent |
| Flush | `mdbx_env_sync_ex()` outside an active write transaction |
| Bounded storage | Explicit geometry plus typed map-full/resize errors |
| Reader diagnostics | `mdbx_txn_info()` lag and retired-space information |

## Rejected Patterns

- Raw `MDBX_env`, `MDBX_txn`, `MDBX_cursor`, DBI handles or return codes in
  Forge public API.
- Zero-copy value views in v1. DB Core returns owned byte vectors.
- MDBX custom comparators. Forge ordering is encoded into stable record keys.
- `MDBX_WRITEMAP`, because it would remove required native savepoints.
- Blocking on MDBX's native writer mutex from an Asio runtime worker.
- Treating a strand as an operating-system thread-affinity guarantee.
- Sharing one native read transaction or cursor between concurrent operations.
- `MDBX_UTTERLY_NOSYNC` as a production configuration.
- Multiple processes or multiple environment opens for the same path in v1.
- Blob payload storage as a reason to choose this backend.
- Reimplementing DB Revision, participant capture, ranked aggregates or Object
  semantics in the driver.

## License And Source Gate

libmdbx 0.13 and later are Apache-2.0. No vendored code may land until the
implementation records all of the following in one commit:

1. exact release and upstream archive URL;
2. archive SHA-256 from an independently verified download;
3. exact included files and proof that they are unmodified;
4. upstream `LICENSE`, `NOTICE` and copyright material;
5. Forge provenance and third-party notice entries.

The docs-only design commit intentionally does not claim a source pin.

## Donor-Derived Test Obligations

- write begin, every operation, nested savepoints and final commit/abort stay on
  one native lane thread;
- coroutine continuation migration on the caller runtime cannot change the
  native write thread;
- concurrent reads through copies of one Core snapshot use separate native
  clones while observing identical data;
- nested transaction behavior matches Core LIFO savepoints;
- `MDBX_MAP_FULL`, `MDBX_READERS_FULL`, `MDBX_TXN_FULL`, corruption,
  incompatibility, busy and I/O errors remain typed;
- `SAFE_NOSYNC` crash testing recovers a valid committed prefix;
- `UTTERLY_NOSYNC` is absent from production configuration paths;
- long-lived reader tests expose lag/file-retention behavior without promising
  cost-free snapshots;
- key limits are queried from the opened environment and checked before native
  CRUD calls;
- scan pagination preserves empty keys, half-open end bounds and continuation
  semantics already required from DB RocksDB.
