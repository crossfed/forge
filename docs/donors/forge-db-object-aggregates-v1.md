# DB Object Ranked Aggregates Donor Baseline v1

This note records the donor evidence for persistent DB Object ranked indexes,
transactional `count`/`sum` maintenance and the ordinal query API. Forge adopts
the proven contracts but keeps the implementation backend-neutral and inside
the existing Core transaction boundary.

## Sources Reviewed

### Boost.MultiIndex 1.90

- `boost/multi_index/ranked_index.hpp`
- `boost/multi_index/detail/rnk_index_ops.hpp`
- ranked-index tests distributed with Boost.MultiIndex

Accepted:

- descriptor names and the `nth`, `rank`, `find_rank`, bound-rank and
  range-rank vocabulary;
- `nth`/`rank` inverse behavior and insertion-position bound ranks;
- the Object ID tie-breaker required to give non-unique persisted entries a
  total order.

Rejected:

- an in-memory-only container as authoritative state;
- runtime comparator overloads, because persisted ordering is part of the DB
  schema and cannot vary per call.

### FoundationDB Record Layer

Reviewed at commit `6a685875a6fff2210c224a5552a310d41ff3ebf7`:

- `fdb-extensions/.../async/RankedSet.java`
- `fdb-record-layer-core/.../indexes/RankIndexMaintainer.java`
- `fdb-record-layer-core/.../indexes/AtomicMutationIndexMaintainer.java`
- the corresponding `RankedSetTest` and `RankIndexTest` suites

Accepted:

- a persistent skip-list with aggregate spans for expected O(log N) rank and
  select;
- deterministic key hashing to choose promoted levels;
- grouped rankings as ranges over one persisted ordered keyspace;
- incremental index maintenance in the same transaction as record mutation;
- explicit format/configuration validation rather than guessing existing
  state.

Forge extends each span from a count to `{count, sum...}`. FoundationDB can use
atomic mutations for standalone count/sum indexes; Forge Core intentionally has
no backend-specific atomic-add primitive, so one checked span plan provides
count, sums and rank operations consistently across all drivers.

Rejected:

- FoundationDB subspaces, tuple codecs and Java futures as public contracts;
- background index building or an implicit scan when aggregate state is
  missing;
- separate physical structures for rank and every sum when one augmented
  structure can answer all required operations.

### Spring / Chainbase

Reviewed Spring commit `e6a99f68b67abc4d89fe716755b2e1394a4991f7`:

- Chainbase object database and generic index integration;
- undo-session index mutation and restoration paths;
- revision commit, squash and undo behavior.

Accepted:

- derived index state changes atomically with the owning object mutation;
- rollback and savepoint-like boundaries restore all derived state;
- index declarations remain compile-time schema, not runtime policy.

Rejected:

- copying Chainbase's in-memory Boost container as a persistent backend;
- blockchain-specific revision or controller concepts in DB Object.

## Forge Decisions

- `ranked_primary_unique`, `ranked_unique` and `ranked_non_unique` opt in
  declaratively beside existing indexes.
- `sum<Tag, Projection, Accumulator>` accepts member/function projections;
  variadic `member` supports nested fields.
- Root totals answer global aggregates in O(1); span subtraction answers
  grouped/range aggregates in expected O(log N).
- Only `nth` materializes one Object. Rank and aggregate operations use index
  records only.
- CRC64-ECMA over the stable logical key selects one of 16 levels.
- Arithmetic and conversion are checked before the first write.
- Missing ranked state on populated storage fails closed with
  `aggregate_rebuild_required`; migration/rebuild is a separate operation.
- `single_writer` reuses the Object writer lane. Backend-managed concurrency
  requires record locks and canonical root-lock order.
- Core transactions, savepoints, snapshots and DB Revision remain the sole
  atomicity and visibility boundaries.

## Tests Required

- Boost parity for order, rank, select and bounds;
- global/group/range count and sums, including nested projections;
- insert, replace, moved keys, erase, rollback, savepoint and Revision revert;
- stable old ranks in snapshots after concurrent commit;
- signed and unsigned overflow without partial writes;
- fail-closed missing state and typed corruption;
- RocksDB reopen and backend record-lock behavior;
- instrumentation proving no Object scan and sublinear ranked reads.
