# Forge Asio Compute And Spring Concurrency Donor Baseline v1

This note records the Spring execution model inspected while defining the
FORGE compute and snapshot concurrency direction. Spring is an architectural
donor, not a runtime dependency or a required internal structure.

## Donor Snapshot

- Repository: local `AntelopeIO/spring` donor checkout.
- Commit: `e6a99f68b`.
- Inspected files:
  - `plugins/producer_plugin/producer_plugin.cpp`;
  - `plugins/chain_plugin/chain_plugin.cpp`;
  - `libraries/chain/controller.cpp`;
  - `libraries/chain/include/eosio/chain/thread_utils.hpp`;
  - `libraries/chain/transaction_context.cpp`;
  - `libraries/chain/include/eosio/chain/config.hpp`;
  - `libraries/chaindb/include/chainbase/chainbase.hpp`.
- Boost.Asio donor:
  - `boost/asio/thread_pool.hpp`;
  - `boost/asio/impl/thread_pool.ipp`.

## Observed Spring Model

- Incoming transaction signature recovery runs on the controller chain thread
  pool.
- After recovery, state-mutating transaction execution is posted back to the
  ordered `read_write` execution queue.
- `controller::push_transaction()` constructs one `transaction_context` and
  executes it synchronously. A non-read-only context owns a Chainbase undo
  session that is squashed on success or undone on failure.
- Spring therefore does not obtain parallel state mutation merely from the
  `chain-threads` option.
- Parallel read-only transactions use a separate read-only pool and explicit
  read windows. The producer plugin switches Chainbase into dynamic read-only
  mode before starting those workers and waits for them before returning to the
  write window.
- The controller pool also serves independent work such as block assembly,
  signature recovery, quorum-certificate verification and voting paths.
- Spring `named_thread_pool` owns named threads, runs per-worker initialization,
  reports worker failures and joins on stop/destruction.

## Accepted Patterns

- Keep mutable state transitions deterministic and explicitly ordered.
- Move independent CPU-heavy preprocessing away from the main I/O/execution
  path.
- Give read-only execution dedicated concurrency and prevent accidental writes.
- Own worker lifetime and shutdown at a runtime/controller boundary.
- Keep thread naming and worker initialization hooks at the pool owner boundary.
- Preserve rollback semantics around each mutable transition.
- Use Boost.Asio `thread_pool` for joined native worker ownership, while keeping
  admission in an explicit bounded Forge FIFO.

## Deliberate FORGE Difference

FORGE DB backends expose immutable Core snapshots and an explicit
single-writer policy. Once Object and Blob views can join the same Core
snapshot, readers do not need a global read window that excludes the writer:

```text
Spring: write window -> stop writes -> parallel read window -> stop reads
FORGE:  snapshot readers run concurrently with an ordered current-state writer
```

This is a stronger concurrency substrate for reusable services:

- an existing reader remains pinned to its captured revision;
- a writer may publish a newer revision without invalidating that reader;
- Object and Blob views can share one physical snapshot and revision;
- the single-writer gate protects mutation ordering without turning the whole
  scheduler into a single-thread executor.

The architecture is documented in
[`compute-and-snapshots.md`](../runtime/compute-and-snapshots.md). The shared DB
read-view direction is documented separately in
[`forge-db-state-services-v1.md`](../iterations/forge-db-state-services-v1.md).

## Rejected Patterns

- Copying Spring's global read/write window switching when backend snapshots
  permit readers and a writer to coexist.
- Treating `chain-threads` as evidence of parallel mutable contract execution.
- Running synchronous VM work on FORGE I/O runtime workers.
- Creating per-plugin or per-VM unmanaged thread pools.
- Moving state conflicts, overlays or deterministic commit policy into the
  generic compute executor.

## Follow-Up Boundary

The compute implementation provides bounded CPU execution and allows parallel
preprocessing and snapshot-based read-only work. Parallel mutable
execution is a separate consumer-layer mechanism requiring private overlays,
dependency tracking, deterministic validation and conflict re-execution.
