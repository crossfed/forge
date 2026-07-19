# Forge Contract SDK Surface Donor Baseline v1

This note records the complete public contract surface accepted from pinned CDT
and Spring. Donor repositories are compatibility oracles, not build
dependencies.

## Donors

- CDT `69599db279b7b93d0688502720c15c6962a1401b`:
  - `libraries/eosiolib/capi/eosio`;
  - `libraries/eosiolib/core/eosio`;
  - `libraries/eosiolib/contracts/eosio`;
  - active ABI generator fixtures under `tests/unit`.
- Spring `e6a99f68b67abc4d89fe716755b2e1394a4991f7`:
  - `libraries/chain/webassembly`;
  - `libraries/chain/include/eosio/chain/webassembly`;
  - contract and API tests under `unittests`.

## Accepted Surface

Intrinsic interface version 1 contains 148 unique functions grouped into
`core`, `database`, `privileged`, `crypto_ext`, `bls`, `call` and
`instant_finality`. Entries carry exact C and WASM signatures plus an optional
Spring protocol-feature identifier. The canonical registry generates modern C
declarations, EOSIO C headers, the typed host skeleton and the import manifest.

The pinned CDT snapshot contains 14 public C headers and 39 public C++ headers.
Forge also installs two canonical C headers and 30 modern leaf modules. EOSIO
headers are aliases, adapters and macros over the same protocol values,
serialization, dispatcher and intrinsic ABI; they do not own a second runtime.

The committed manifest at `guest/tests/tooling/sdk_surface_manifest.json`
records the exact header and module inventories, 13 canonical attributes, 33
ABI vocabulary entries and stable contract-visible errors. The checker compares
that independent inventory with installed SDK artifacts and the intrinsic
golden manifest.

The `surface` contract compiles every donor C++ header and every modern module
in its own translation unit. Those units share one module dependency graph, so
the gate proves standalone source usability without rebuilding module
interfaces for each header.

## Executable Oracle

`guest/tests/host` registers every intrinsic directly from the canonical macro
registry. Database behavior uses Forge ObjectDB. Hashing, recovery, BN254 and
BLS use Forge crypto. The remaining capability families use explicit,
deterministic invocation state. Successful execution commits; assertion, VM or
host failure restores both ObjectDB and all observable side effects.

This host proves SDK behavior but is not a blockchain controller. RAM billing,
consensus, fork choice, producer policy and durable product schemas remain in
the blockchain runtime.

## Rejected Donor Mechanisms

- patched Clang and CDT compiler extensions;
- duplicated EOSIO serialization, crypto values or chain types;
- chainbase and Spring controller state inside the SDK;
- host-native `long double` or unchecked guest pointers;
- hand-maintained intrinsic registration lists.

The next acceptance block compiles the unchanged Spring contract corpus. Any
missing fundamental API found there is a defect in this foundation, not planned
scope for a new SDK layer.
