# Forge Contract Database Intrinsics Donor Baseline v1

This note records the Spring and CDT behavior used for the Contract SDK
database intrinsic interface. Forge owns a declarative C and WebAssembly ABI;
the donors remain test oracles and are not build dependencies of the SDK.

## Donor Snapshots

- CDT commit `69599db279b7b93d0688502720c15c6962a1401b`:
  - `libraries/eosiolib/capi/eosio/db.h`;
  - `tests/unit/test_contracts/capi/db.c`.
- Spring commit `e6a99f68b67abc4d89fe716755b2e1394a4991f7`:
  - `libraries/chain/webassembly/database.cpp`;
  - `libraries/chain/include/eosio/chain/apply_context.hpp`;
  - `libraries/chain/apply_context.cpp`;
  - `libraries/chain/webassembly/cf_system.cpp`;
  - `libraries/chain/webassembly/runtimes/eos-vm.cpp`;
  - `unittests/test-contracts/test_api_db/test_api_db.cpp`;
  - `unittests/test-contracts/test_api_multi_index/test_api_multi_index.cpp`;
  - `unittests/contracts/test_wasts.hpp`;
  - `unittests/api_tests.cpp`.

## Accepted Interface

Interface version 1 contains 60 database functions:

- 10 primary 64-bit table functions named `db_*_i64`;
- 10 secondary-index functions for each of `idx64`, `idx128`, `idx256`,
  `idx_double` and `idx_long_double`.

The C declarations preserve donor constness and integer widths. WebAssembly
pointers are `i32`; account, scope, table, payer and primary-key values are
`i64`; iterators, lengths and return codes are `i32`. The historical
`db_get_i64` output parameter remains `const void*`. The `idx256` `data_len`
parameter counts 128-bit elements rather than bytes.

The canonical registry generates `<forge/contract/intrinsics.h>`, the thin
`<eosio/db.h>` veneer, the host interface and `intrinsics.json`. The checked-in
`spring_db_intrinsics.txt` fixture is an independent list of expected WASM
signatures and is not generated from the Forge registry.

The complete contract interface now contains the exact 152-function union of
the pinned CDT and Spring snapshots. The 60 functions in this note remain the
`database` capability set. `current_receiver` and the other contract families
are recorded by `forge-contract-sdk-surface-v1.md` and the committed SDK surface
manifest.

## Executable Test Oracle

`guest/tests/host` provides a non-installed executable oracle over
`forge.db.object`. It is intentionally narrower than a blockchain host:

- one ObjectDB transaction spans one WASM invocation;
- successful `apply` and `eosio_exit` commit;
- assertion, DB and VM failures roll back;
- ranked ObjectDB indexes implement bounds, traversal and uniqueness;
- the iterator cache is discarded after every invocation;
- a test-local Core driver stores only ordered records and contains no table or
  iterator logic.

The Spring-to-Forge scenario mapping is:

| Spring donor case | Forge proof |
|---|---|
| `primary_i64_general`, `primary_i64_lowerbound`, `primary_i64_upperbound` | `database_host_commits_primary_and_secondary_objectdb_state` scenario 0 |
| `idx64_general`, `idx64_lowerbound`, `idx64_upperbound` | scenario 1 preserves the donor data set, operation order, duplicate-secondary traversal and bound results |
| `idx128_general` and the `test_api_multi_index` modify-order case | scenario 1 traverses the donor ordering after updating primary key 3 |
| `idx256_general` and duplicate-secondary traversal | scenario 1 verifies two-word keys, bounds, duplicate ordering and removal |
| `idx_double_general` plus `test_api_multi_index` floating ordering | scenario 1 traverses the donor ten-row order and verifies lower/upper bounds |
| `idx_long_double_general` plus `test_api_multi_index` floating ordering | scenario 1 repeats the donor order with fixed 128-bit representations |
| aligned and unaligned overlapping lower/upper-bound output pointers in `test_api_db` | scenario 1 verifies Spring's primary-then-secondary host write order |
| `test_invalid_access` and iterator-cache checks in `apply_context` | `database_host_rejects_foreign_iterators_and_resets_iterator_cache` and wrong-kind scenario 16 |
| `idx_double_nan_create_fail`, `idx_double_nan_modify_fail`, `idx_double_nan_lookup_fail` | invalid scenarios 9 and 12-15 |
| `misaligned_secondary_key256_tests` | successful scenario 11 |
| `api_tests.cpp::db_tests` failure rollback plus `cf_system.cpp`/`test_wasts.hpp` `eosio_exit` | `database_host_rolls_back_assertions_and_commits_exit` |

The mappings preserve the donor operation order and observable result while
using Forge ObjectDB models instead of chainbase. They are not copied Catch2 or
Boost test bodies.

The C++ compatibility corpus is recorded in
`guest/tests/tooling/multi_index_donor_mapping.json`. Every active action in
Spring's `test_api_multi_index.cpp` maps to an executable modern and EOSIO
scenario. The gate also compares the generated ABI for each modern/EOSIO pair.

`db_get_i64` also follows Spring's historical return contract: a zero-sized
read reports the stored value size, while a non-zero read returns the number of
bytes actually copied. The executable fixture covers both forms and truncated
reads.

CDT `multi_index::modify` updates a secondary row only when the extracted
secondary key changes. A payer-only modification therefore updates the primary
row payer while preserving the existing secondary-row payer. Forge keeps this
observable behavior for Spring/CDT RAM-billing compatibility and covers it with
an ObjectDB-backed contract regression.

## Product Runtime Boundary

The future blockchain host binding remains responsible for:

- iterator lifetime, end iterators and invalid iterator errors;
- write authorization and RAM payer accounting;
- table ownership and receiver semantics;
- the `idx256.data_len == 2` requirement;
- NaN rejection and deterministic floating secondary-key ordering.

The executable oracle proves all items except authorization and RAM accounting;
it does not make these policies part of Forge. `multi_index` and `singleton`
are guest templates over the C ABI; product host bindings remain separate work.
The guest API remains independent of `forge.db`.

## Verification

- The unchanged CDT `capi/db.c` fixture compiles through generated
  `<eosio/db.h>` and retains all 60 imports.
- `forge.vm.wasm` parses the fixture and verifies exact names, parameter types
  and return types against the independent golden manifest.
- Compile-time assertions cover representative primary, 128-bit, 256-bit and
  floating signatures in the generated host interface.
- Existing modern and legacy contracts execute without acquiring unused
  database imports.
- A generated contract calls all 60 DB functions through `forge.vm.wasm` and
  validates committed ObjectDB state independently of the host iterator cache.
- Rollback, duplicate keys, ownership, payer, wrong-kind/stale iterators,
  `idx256` length and alignment, and floating NaN are executable regressions.
- Modern and EOSIO `multi_index` corpora produce identical ABI, action return
  bytes and complete committed ObjectDB snapshots.
