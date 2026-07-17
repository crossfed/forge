# Forge Contract SDK And Toolchain Design Baseline V1

Status: accepted design baseline; the first build-foundation vertical is
implemented on `contract-sdk-toolchain-v1`.

## Implementation Snapshot

The first vertical is split by ownership: wasm32 code and SDK assembly live in
`guest/`, reusable host services live in `libraries/contract`, and thin command
entry points live in `tools/`. It currently delivers:

- release and developer toolchain profiles;
- pinned upstream libc++, libc++abi and compiler-rt sysroot construction;
- target-neutral raw codec and guest-safe chain value modules;
- contract context, generated dispatcher and the initial versioned intrinsic set;
- complete Spring/CDT database C ABI in intrinsic interface v1;
- modern Forge and minimal EOSIO source vocabularies;
- Clang attribute plugin, ABI generator, structural checker and build manifest;
- CMake `find_package(ForgeContract)` and `forge_add_contract()`;
- allocator/runtime tests and execution in `forge.vm.wasm`;
- reproducibility, negative validation, archive and relocation gates.

The unchanged EOSIO C++ header corpus, `multi_index`, executable database host
bindings and the full legacy contract corpus remain the next compatibility block. Statements
below describing those surfaces are accepted target architecture, not claims
that the first vertical already ships them.

This document defines only the guest SDK and contract toolchain track.
`forge.vm.wasm` is already released and is the execution and validation oracle
for this work. VM engine development, a Rust SDK and blockchain-owned host
binding implementations are separate tracks.

## Non-Negotiable Boundaries

These decisions prevent the implementation from drifting away from the agreed
design:

1. Forge does not invent a C or C++ standard library. The contract sysroot is
   built from pinned upstream LLVM libc++, compiler-rt and the minimum required
   runtime shims. V1 includes the main standard-library functionality needed by
   production smart contracts, including owning strings and vectors.
2. The intrinsic registry is `FORGE_API`-style only in the sense that it is a
   declarative, typed source of truth. It does not import, link or otherwise
   depend on the host-oriented `forge.api` library.
3. Legacy compatibility does not mean carrying Spring CDT's patched compiler.
   The toolchain uses unmodified, pinned upstream Clang and lld from day zero.
   Spring/CDT supplies compatibility behavior, fixtures and contract corpora,
   not compiler code or patches.
4. Legacy EOSIO and modern Forge contract APIs are two source vocabularies over
   one implementation, one attribute canon, one ABI and one intrinsic surface.
5. Guest code is compiled only for `wasm32`, never linked into Forge host
   binaries, and meets the host only at runtime through `forge.vm.wasm`.

## Principles

### Zero Compiler Patches

Use vanilla pinned Clang 19 or newer and lld. Functionality that legacy CDT
implemented through a compiler fork belongs in the toolchain configuration,
sysroot, host tools or guest libraries.

### One World, Two Vocabularies

Legacy contracts use:

```cpp
#include <eosio/eosio.hpp>

namespace eosio {
// ...
}

[[eosio::action]]
```

Modern contracts use:

```cpp
import forge.contract;

namespace forge::contract {
// ...
}

[[forge::action]]
```

Both surfaces lower to the same canonical representation. Tooling must not
produce different ABI or execution behavior based on which vocabulary was
used.

### Executable Compatibility

Compatibility means contract-observable behavior. It is defined by an
executable reference corpus, ABI fixtures, WASM import manifests and VM
execution results, not by source resemblance to CDT.

### Interface Ownership

Forge owns the versioned intrinsic interface contract. A blockchain product
implements host bindings against the generated skeleton. Forge does not own
the product's controller, state or host-binding implementation.

## Repository Layout

```text
forge/
  libraries/                    # Host libraries and dual-target source owners
    raw/                        # One serialization implementation
    chain/                      # Selected protocol values shared with guests
    vm/wasm/                    # Released host VM engine
    asio/                       # Host-only runtime libraries
    contract/                   # Empty host tooling family
      abi/                      # AST, ABI and dispatcher generation
      attributes/               # Clang attribute registration
      validation/               # ABI/WASM/import validation
      manifest/                 # Deterministic build manifests
  guest/                        # Self-contained wasm32 CMake project
    libraries/
      runtime/                  # allocator and freestanding shims
      contract/                 # forge.contract.* implementation
      eosio/                    # compatibility veneer only
    examples/                   # SDK example contracts
    tests/                      # Contracts executed through forge.vm.wasm
    sysroot/                    # Pinned LLVM-based freestanding sysroot
    cmake/                      # Toolchain and add_contract()
  tools/
    abigen/                     # Thin ABI command entry point
    attr-plugin/                # Thin Clang plugin entry point
    contract-check/             # Thin validation command entry point
    contract-manifest/          # Thin manifest command entry point
```

The normal host build never enters `guest/` or `tools/`. Optional host contract
libraries are built only with `FORGE_ENABLE_CONTRACT_TOOLING=ON`. The guest
build is a separate CMake project driven by the WASM toolchain. Dual-target
libraries remain under `libraries/`; the guest project compiles the same
sources for `wasm32`.

C++ module binary interfaces are target-specific and are never distributed.
The SDK distributes module sources.

## Vanilla Clang Toolchain

`guest/cmake/ForgeContractToolchain.cmake` owns the contract compilation policy:

- `--target=wasm32`, not WASI;
- `-nostdlib`;
- exceptions disabled;
- RTTI disabled;
- thread-safe statics disabled;
- pinned lld flags, including `--no-entry`, `--stack-first`, stack size,
  exported `apply` and import handling;
- reproducibility flags such as `-ffile-prefix-map` and removal of timestamps.

### Consensus Feature Pinning

Modern Clang may enable post-MVP WebAssembly features by default. Generated
contracts must use `-mcpu=mvp` plus an explicit allowlist of features that
matches chain validation.

Every produced contract is validated and executed by the released
`forge.vm.wasm`:

```text
compiler feature surface = VM validation surface = chain consensus surface
```

## Canonical Attributes

Vanilla Clang drops unknown attributes from the abstract syntax tree. Legacy
source files contain literal attributes such as `[[eosio::action]]`, so macro
substitution alone cannot preserve the corpus.

The canonical retained form is:

```cpp
[[clang::annotate("forge.action")]]
[[clang::annotate("forge.on_notify", "account::action")]]
```

`attr-plugin` is a standard Clang `ParsedAttrInfo` plugin loaded with
`-fplugin`. It registers both EOSIO and Forge spellings and lowers them to the
same annotation payload. It is built against the pinned Clang release and is
not a compiler fork.

For IDE and clangd operation without the plugin, contract compile flags may use
`-Wno-unknown-attributes`; the attributes do not affect ordinary code analysis.
The V1 Windows baseline is WSL unless a reliable native plugin-loading path is
accepted later.

## Legacy EOSIO Veneer

Modules cannot export macros, so the legacy surface uses thin headers. Each
`<eosio/*.hpp>` file contains only:

- imports of the corresponding Forge contract modules;
- targeted `using` declarations in `namespace eosio`;
- legacy macros such as `ACTION`, `TABLE`, `CONTRACT` and `EOSIO_DISPATCH`.

The veneer contains no implementation logic and never uses `using namespace`.
EOSIO C API headers such as `<eosio/db.h>` are generated from the intrinsic
registry.

Explicit `EOSIO_DISPATCH` instantiates the Forge template dispatcher.
Attribute-only contracts use `abigen --gen-dispatch` to generate the
`apply` translation unit when the contract defines neither `apply` nor the
dispatch macro.

Mixing legacy headers and modern modules in one contract is supported. This is
an incremental migration path, not a second runtime.

## Intrinsic Registry

The intrinsic registry is an independent declarative schema for host functions,
versioned by protocol-feature interface sets. `FORGE_API`-style describes its
typed declaration model only. The registry has no dependency on the host
`forge.api` family.

One registry generates:

1. the guest C API module with `extern "C"`,
   `[[clang::import_module("env")]]` and `[[clang::import_name("...")]]`;
2. EOSIO C header veneers;
3. the host-binding skeleton consumed and implemented by the blockchain repo;
4. a compatibility manifest used by import and protocol-feature checks.

This is the single source of truth for names, signatures and interface
versions.

Interface version 1 currently contains seven lifecycle/action-data functions
and all 60 Spring/CDT database functions. The distributed DB portion is a
declarative ABI, not a storage implementation. A non-installed executable test
host validates that ABI against `forge.db.object`; its exact donor surface and
runtime scenario mapping are recorded in
`docs/donors/forge-contract-db-intrinsics-v1.md`.

## Dual-Target Libraries

A Forge library may compile for the guest target only when:

1. its dependency graph is freestanding-clean, with no operating system,
   threads, filesystem or wall-clock facilities;
2. it throws only through Forge's exception-policy macro.

### Raw Serialization

`forge::raw` is the flagship dual-target library. Host and contract code use
one implementation and the same golden byte vectors.

`FORGE_POLICY_THROW_EXCEPTION` is the thin policy wrapper over
`FORGE_THROW_EXCEPTION` and is the dual-target boundary:

- host build: typed Forge exception;
- guest build: non-returning check or abort intrinsic, which aborts the
  transaction.

The remaining bare `throw std::out_of_range` in raw datastream must be routed
through this policy boundary. Dual-target code must not add catch blocks.

### Chain Values

The guest-compatible subset of `forge::chain` includes values contracts can
observe or construct:

- names, symbols and assets;
- time values and checksums;
- permission levels;
- action and transaction shapes needed by contract APIs such as
  `read_transaction`.

Blocks, consensus, fork management and other node-only types remain host-only.

## Contract Sysroot

The sysroot is a pinned LLVM-derived freestanding environment, not a custom
standard library. Forge builds and packages the required upstream libc++,
compiler-rt and libc functionality for `wasm32`, with exceptions and host
operating-system facilities disabled.

V1 includes the standard-library functionality critical to real smart
contracts:

- type traits and concepts needed by the SDK;
- tuple, optional, span and string view;
- the required algorithms and iterator facilities;
- owning `std::string` and `std::vector` using the guest allocator;
- supporting utility, limits and numeric facilities required by the corpus;
- compiler-rt builtins, including 128-bit floating-point operations needed by
  long-double secondary-index behavior.

Runtime shims provide linear-memory allocation, `operator new`, `malloc`,
`memcpy`, `memset`, required string primitives and `abort` mapped to the check
intrinsic. These are target integration shims, not substitute STL containers or
algorithms.

The exact V1 surface is validated against the production contract corpus and
expanded when contract-critical functionality is missing. A later V2 may ship a
broader or full wasm32 libc++ configuration with exceptions still disabled.

The sysroot intentionally exposes no threads, random device, wall clock,
filesystem, sockets or WASI services. CI rejects `wasi_snapshot_*` and every
other import not present in the approved intrinsic manifest.

## Guest Libraries

### `forge.guest.runtime`

Owns:

- guest allocator and linear-memory runtime;
- panic, abort and check integration;
- `__wasm_call_ctors` and exported `apply` entry glue;
- required libc memory shims.

### `forge.contract`

Owns:

- generated intrinsic C API modules;
- `multi_index`, ported from CDT to pure C++23 while preserving its public API;
- template dispatch;
- datastream integration over `forge::raw`;
- EOSIO compatibility veneer headers.

The `multi_index` port does not carry Boost.Hana or CDT compiler dependencies.
Its donor provenance and license attribution are recorded under the normal
Forge provenance rules.

### Safety Profile

The SDK adds production safety beyond CDT parity:

- checked arithmetic suitable for financial values;
- span-first and bounds-checked access that fails through the check intrinsic;
- an optional development profile using minimal UBSan mapped to deterministic
  contract failure;
- a contract-focused clang-tidy profile, with optional checks implemented as a
  standard plugin rather than a compiler fork.

## Contract Build Pipeline

`add_contract()` performs this pipeline:

```text
sources
  |-- pinned clang + attr plugin + wasm32 feature policy --> objects
  |-- abigen reads canonical annotations -----------> ABI
  `-- abigen --gen-dispatch, when required ----------> dispatch.cpp

objects + optional dispatch.cpp
  `-- pinned wasm-ld ---------------------------------------> contract.wasm

contract.wasm
  |-- forge.vm.wasm validation and execution
  `-- approved-import manifest check
```

## Distribution

The Forge Contract SDK distribution is hermetic and contains:

- pinned, unmodified Clang and lld binaries;
- the wasm32 sysroot;
- prebuilt `sysroot/lib/libforge_guest_runtime.a`;
- guest module sources;
- dual-target Forge library sources;
- `abigen` and `attr-plugin`;
- CMake toolchain and `add_contract()` support.

Contract developers consume the SDK artifact and do not need a Forge source
checkout. The release manifest identifies:

```text
{ Clang pin, sysroot version, intrinsic interface version }
```

This version triple is part of reproducible source-to-WASM verification.

The input sysroot is copied to a build-owned staging directory before the guest
runtime is compiled and installed. Contract consumer builds link that archive
before libc++/libc++abi/compiler-rt and never compile runtime sources. The
archive participates in the sysroot hash; runtime `.cpp` and private `.hxx`
files are not distributed.

## Repository Boundaries

This track does not implement:

- blockchain-owned intrinsic host bindings;
- controller, state, system contracts or a Tier B testchain;
- VM engine changes;
- a Rust SDK.

`abigen` and `attr-plugin` are host tools under `tools/`, not guest
libraries. Tier A contract tests use the released VM with mock intrinsic
implementations and do not alter the engine.

## Acceptance Gates

### Day-Zero Gate

An empty or hello contract builds through `add_contract()`, validates and
executes in the released `forge.vm.wasm`.

### Compatibility Corpus Gate

Reference contracts such as `eosio.token`, `eosio.system` and selected
third-party contracts compile unchanged through the EOSIO veneer. Their ABI is
byte-identical to the accepted CDT output and their observable behavior passes
the VM runner fixtures.

The donor corpus is a compatibility oracle. It does not make patched CDT or its
compiler fork part of the implementation.

### Import Gate

Every produced WASM module contains only imports declared by the selected
intrinsic interface manifest.

### Feature Gate

Every produced WASM module validates under the exact chain-accepted WebAssembly
feature set.

### Dual-Target Gate

Host and guest builds of `forge::raw` pass identical golden byte vectors.

## Implementation Order

1. Add the toolchain skeleton, consensus feature pinning, minimum guest runtime
   and day-zero contract gate.
2. Add the independent intrinsic registry and generate the guest C API, EOSIO C
   headers, blockchain host skeleton and compatibility manifest.
3. Make `forge::raw` and the selected `forge::chain` values dual-target, with
   host/guest golden vectors.
4. Port `multi_index` and implement the common dispatcher.
5. Add `attr-plugin`, `abigen`, dispatch generation and complete
   `add_contract()` end-to-end behavior.
6. Add EOSIO veneer headers and the unchanged legacy contract corpus gate.
7. Add the safety, clang-tidy and development UBSan profiles.

The first build-foundation implementation completed steps 1-3 and 5, plus the
database C ABI portion required before step 4. The executable test host now
proves that ABI over Forge ObjectDB. The next compatibility block is C++23
`multi_index` and `singleton` over the canonical C ABI.

Each stage must leave one executable acceptance proof. Compatibility is not
deferred to the final stage.

## Open Decisions

- Modern source spelling `[[forge::action]]` is assumed and must use the same
  attribute plugin and canonical payload as `[[eosio::action]]`.
- Windows V1 uses WSL unless native plugin loading is accepted separately.
- The V1 libc++ surface is contract-critical and corpus-driven; it is not
  restricted to trivial freestanding vocabulary and does not become a
  hand-written STL.
- Floating-point use receives a contract lint warning rather than a blanket
  language ban.
