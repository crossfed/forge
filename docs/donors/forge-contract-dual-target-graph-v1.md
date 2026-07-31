# Forge Native Guest Project Donor Baseline v2

This note replaces the earlier cross-config descriptor design. A product owns
one native CMake configuration and one standalone guest CMake configuration.
The ordinary target graph inside each configuration is authoritative.

## Accepted Donors

### CDT

Pinned CDT commit `69599db279b7b93d0688502720c15c6962a1401b` is the donor for
direct wasm32 project configuration, contract compilation, ABI generation and
dispatcher semantics. Contract sources and their libraries are configured
together under the guest toolchain.

Forge extends that model only by allowing selected protocol source directories
to be added to a separate native project. The native and guest builds compile
the same physical `.cppm` and `.cpp` files independently.

### CMake And Clang

Standard CMake cross-compilation provides the implementation:

- a toolchain file selects wasm32 before `project()`;
- `add_subdirectory()` adds shared and guest-only libraries;
- `FILE_SET CXX_MODULES` and Clang scanning build the module dependency graph;
- ordinary PUBLIC and PRIVATE links define target visibility;
- compiler depfiles track textual includes;
- an optional `ExternalProject` launches the standalone guest project without
  inspecting its targets.

No host archive, BMI or PCM file crosses into the guest build.

Cargo host/target units and Bazel configured targets support the conceptual
point that one logical source package may have independent configured
compilations. Forge does not import either build system or their metadata.

## Forge Helpers

`forge_add_contract_library` is a thin declaration helper for an ordinary
static CMake library. It adds module sources, implementation sources,
PUBLIC/PRIVATE dependencies and guest compile settings when the active
configuration targets wasm32. The concrete target is private and the declared
name is an immutable alias, so later CMake calls cannot bypass ABI ownership
metadata.

`forge_add_contract` runs only inside the guest project. It generates the ABI
and dispatcher, links and validates WASM, and writes the runtime manifest.

`forge_add_contract_project` is an optional host launcher. It configures and
builds the same standalone guest directory and exposes artifact paths for
native VM tests.

## ABI Metadata

Abigen consumes compiler metadata from object files built in the current guest
configuration. Forge passes stable owner IDs and dependency scopes from the
same helper calls that create the CMake links. This metadata is used only to
diagnose invalid public/private module imports; it is not a build graph and is
not transported to another configuration.

The SDK module registry is complete and fail-closed: an imported known SDK
module must belong to a visible owner. Standalone Abigen calls without contract
library metadata retain the existing source-analysis behavior.

## Runtime Manifest

Manifest schema v3 records:

- Forge SDK and LLVM identity;
- sysroot and intrinsic interface identity;
- reproducibility profile;
- WASM imports and features;
- WASM and ABI SHA-256 values.

It intentionally contains no source graph and makes no source-verification
claim. Etherscan-like verification is deferred to a separate attestation layer
based on a hermetic build and actual compiler dependencies.

## Rejected Mechanisms

- host-to-guest JSON graph transport;
- target fingerprints or reconciliation;
- reverse parsing of `LINK_LIBRARIES`, `INTERFACE_LINK_LIBRARIES`,
  `$<LINK_ONLY:...>` or directory wrappers;
- central host-target to guest-target mappings;
- downstream dual-target source-package materialization;
- installed BMI or PCM transport;
- source attestation derived from a manual file inventory.

## Validation

Focused validation must cover:

- native and wasm32 compilation of the same module sources;
- transitive PUBLIC and PRIVATE module visibility;
- rejection of private re-exports and host-only guest dependencies;
- direct and launcher artifact identity;
- multi-config Release forwarding;
- named action direct ABI layout and Raw byte parity;
- VM persisted-state roundtrip;
- relocated Contract SDK consumption;
- Spring/EOSIO compatibility corpus.
