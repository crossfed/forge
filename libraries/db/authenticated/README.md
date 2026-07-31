# forge_db_authenticated

`forge_db_authenticated` is Forge's persistent authenticated ordered-state
library. It creates immutable path-copied AVL nodes, content-addressed values,
version roots and independently verifiable point proofs over a DB Core driver.

## Stability

The C++ API, proof schema and persisted node format are **Experimental** until
the external cryptographic review required by the first production consumer is
complete. A release that changes any of those contracts must identify the
affected schema version and require an explicit database reset or migration.

## Package

- CMake target: `forge_db_authenticated`
- package component: `db_authenticated`
- namespace: `forge::db::authenticated`
- modules: `forge.db.authenticated.types`, `.hash`, `.proof`, `.codec`,
  `.standards`, `.store`, `.transaction` and `.exceptions`

The library depends on DB Core and Forge SHA-256. It does not depend on a
concrete DB driver, ObjectDB, a blockchain protocol or an API transport.

## Transactions

Join the authenticated participant before the first mutation or savepoint:

```cpp
auto db_tx = co_await driver->begin_transaction();
auto authenticated_tx = co_await authenticated.join(db_tx, block_number);

const auto preview = co_await authenticated_tx.preview(changes);
co_await authenticated_tx.stage(changes, preview.commitment.state_root);
co_await db_tx.commit();
```

`preview()` performs no writes. `stage()` writes immutable nodes, values and the
new version root through the caller's normal DB Core transaction. Revision
capture therefore sees the authenticated records and restores the previous root
during a reorg. The participant rejects commit if no version was staged.

A product that projects ObjectDB changes registers a transaction-scoped
`forge::db::object::precommit_observer`. The observer receives the final ObjectDB
change set after savepoint rollback and must verify that it matches the staged
authenticated mutation digest. Projection policy remains downstream because
Forge does not know product table or index semantics.

## Hash Schema

Schema v3 uses SHA-256 with explicit domain and length framing. Inner nodes
authenticate their full ordered interval in addition to the separator:

```text
value = H(value-domain | length | value)
leaf  = H(leaf-domain | tree-domain | key | value-hash)
inner = H(inner-domain | tree-domain | height | size | min-key | max-key |
          separator | left-hash | right-hash)
```

The canonical tree domain is `role-tag | base-domain`, where the leading byte is
`0x01` for state and `0x02` for changes. The role is therefore injective and is
never derived by appending a textual suffix to a caller-controlled domain.

For every expanded inner node, verification requires `min-key = left.min-key`,
`max-key = right.max-key`, `separator = right.min-key` and
`left.max-key < right.min-key`. Height, size and ordered bounds are required for
ranked range proofs; this metadata is consensus-relevant, not a storage cache.

Schema v3 uses persisted node/root format version 4 and is intentionally
incompatible with every earlier experimental format. There is no compatibility
reader or mode; existing experimental stores must be reset or explicitly
migrated before opening them with this version.

## Standards Boundary

`forge.db.authenticated.standards` owns only the dependency-neutral adapter
contract and capability declaration. Forge does not currently ship an official
Cosmos ICS23 protobuf implementation, so `cosmos_ics23_v1` reports no native
codec or verifier. It must be implemented by an adapter backed by the official
`cosmos/ics23` schema and verifier; Forge proof DTOs must not be relabelled as
ICS23.

Official IAVL existence/non-existence vectors and a pinned Go verification
harness live under `tests/db_authenticated/ics23_harness`. They establish the
cross-language conformance boundary but do not claim that Forge hash schema v3
is presently representable by the IAVL `ProofSpec`.

## Boundaries

Range/change multiproofs, pruning and garbage collection are part of the same
library but are not yet declared production-ready. Legacy IAVL `RangeProof` is
not a supported format. Proof parsing is bounded by explicit key, value, depth,
node and exact serialized-byte limits. Proof depth has an implementation hard
cap of 256, independent of caller settings; this is comfortably above the
maximum valid AVL height for a tree whose rank and size are `uint64_t`.

Range generation tracks the exact encoded size as each node is appended,
including varuint count-prefix growth. Before loading another value it first
checks the minimum remaining framing budget, and a fetched value is rejected
before caching or copying when its exact encoded size does not fit. Decoding
binds untrusted collection counts to the remaining payload's minimum canonical
element size before any reserve or growth.

## Benchmark

`benchmark_forge_db_authenticated` is an MDBX-backed executable and is not
registered with CTest. Its production baselines use one million and ten million
ordered keys with 32-byte values:

```sh
cmake --build build/release --target benchmark_forge_db_authenticated -j4
build/release/tests/benchmark_forge_db_authenticated \
   --keys 1000000 --value-bytes 32 --path /tmp/forge-authenticated-1m
build/release/tests/benchmark_forge_db_authenticated \
   --keys 10000000 --value-bytes 32 --path /tmp/forge-authenticated-10m
```

A short disposable smoke invocation is:

```sh
build/release/tests/benchmark_forge_db_authenticated --keys 10000 --value-bytes 32
```

`--path` must not exist before the run and is preserved afterward. Omitting it
uses and removes a temporary directory. The initial-batch timer covers MDBX
transaction creation, authenticated staging and durable commit, but excludes
client-side mutation construction. Point and range timings cover public proof
generation, including snapshot acquisition; proofs omit values so value size
does not make the fixed 256-item range exceed the proof byte limit.

The executable writes one JSON document to stdout. It reports the committed
state/change roots and sizes, initial-batch milliseconds and keys/second, 1,000
point-proof milliseconds/proofs-per-second/average wire bytes, and 100 ranked
range-proof milliseconds/proofs-per-second/average wire bytes/average nodes.
The range limit is fixed at 256. Configuration output also records the MDBX map
ceiling and growth step used by the run.

The hostile-input parser/verifier harness is opt-in and uses Clang libFuzzer,
AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build/fuzz -G Ninja \
   -DFORGE_DB_AUTHENTICATED_ENABLE_FUZZ_TESTS=ON
cmake --build build/fuzz --target forge_db_authenticated_fuzz -j4
build/fuzz/tests/forge_db_authenticated_fuzz -max_total_time=60
```
