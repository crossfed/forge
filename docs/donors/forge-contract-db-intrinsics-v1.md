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
  - `libraries/chain/webassembly/runtimes/eos-vm.cpp`;
  - `unittests/test-contracts/test_api_db/test_api_db.cpp`.

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

## Deferred Runtime Behavior

This block intentionally does not implement database execution. The blockchain
host binding remains responsible for:

- iterator lifetime, end iterators and invalid iterator errors;
- write authorization and RAM payer accounting;
- table ownership and receiver semantics;
- the `idx256.data_len == 2` requirement;
- NaN rejection and deterministic floating secondary-key ordering.

`multi_index`, `singleton`, an executable test database and product host
bindings are separate follow-up work. Forge does not depend on `forge.db` for
the guest C ABI.

## Verification

- The unchanged CDT `capi/db.c` fixture compiles through generated
  `<eosio/db.h>` and retains all 60 imports.
- `forge.vm.wasm` parses the fixture and verifies exact names, parameter types
  and return types against the independent golden manifest.
- Compile-time assertions cover representative primary, 128-bit, 256-bit and
  floating signatures in the generated host interface.
- Existing modern and legacy contracts execute without acquiring unused
  database imports.
