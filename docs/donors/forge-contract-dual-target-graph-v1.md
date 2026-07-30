# Forge Native Guest Project Donor Baseline v1

This note defines the build model for Forge contract protocol libraries. One
physical source tree may be compiled in two independent CMake configurations:
the product host configuration and the product guest configuration. Each
configuration owns an ordinary CMake graph for its selected toolchain.

The guest configuration is the source of truth for the deployable contract. A
host configuration may launch that project, but it must not serialize and
reconstruct the guest target graph.

## Accepted Donors

### CDT

Pinned CDT commit `69599db279b7b93d0688502720c15c6962a1401b`
defines the accepted direct guest-project model, wasm32 compilation, ABI
generation and dispatcher semantics. Product contract sources and libraries are
configured together under the contract toolchain.

Forge additionally supports compiling selected protocol sources in a separate
native configuration. This extension does not change the CDT-derived guest
ownership boundary.

### CMake

Standard CMake cross-compilation supplies the implementation model:

- a toolchain file selects wasm32 before `project()`;
- `add_subdirectory()` adds shared and guest-only source libraries directly;
- `FILE_SET CXX_MODULES` owns module interface inputs;
- `install(EXPORT ... CXX_MODULES_DIRECTORY ...)` exports native package
  metadata without installing BMI or PCM files;
- `ExternalProject` may launch a standalone guest project from a host build,
  but does not describe or recreate its targets.

The host and guest configurations may produce different object files from the
same physical sources. Host objects, archives and module artifacts are never
reused by the guest configuration.

### Cargo And Bazel

Cargo host/target units and Bazel configured targets justify one narrow
invariant: logical identity is independent from one configured compilation.
Forge adopts explicit identities and dependency scopes, but imports neither
build system and does not exchange their metadata formats.

## Configured Contract Libraries

`forge_add_contract_library` declares a complete library in the current CMake
configuration:

- stable library ID;
- source root, module roots and files with explicit roles;
- public and private dependencies;
- one concrete static target and one immutable public alias.

Calling the declaration from a native project produces a native library.
Calling the same declaration from a guest project produces a wasm32 library.
The two calls use the same physical source files but do not communicate through
generated target metadata.

Guest-only implementation libraries use the same declaration and normal Forge
library layout. Their CMake directories are added only by the guest project.

## Direct Contract And Optional Launcher

`forge_add_contract` is valid in the guest configuration. It creates the WASM
target, ABI generation, validation and manifest commands directly from targets
already present in that configuration.

`forge_add_contract_project` is a host-side convenience launcher. It configures
the product's standalone guest directory with the Forge toolchain and builds a
named contract. It exposes artifact paths so native VM tests can depend on the
result. It must not inspect source targets, dependency properties or module
closures from the host configuration.

The following invocations build the same guest project:

```sh
cmake -S guest -B build/guest -DCMAKE_TOOLCHAIN_FILE=<ForgeContractToolchain>
cmake --build build/guest --target product_artifacts
```

```sh
cmake --build build/host --target product_guest
```

## Descriptor Ownership

Forge still records an explicit descriptor in each active configuration. The
descriptor validates IDs, file roles, dependency scopes and cycles, and drives
ABI input and source attestation for that configuration.

It is not a cross-toolchain build protocol. In particular, Forge does not:

- read `LINK_LIBRARIES`, `INTERFACE_LINK_LIBRARIES`, `$<LINK_ONLY:...>` or
  directory wrappers;
- serialize physical host targets for a guest build;
- infer guest target names from host target names;
- run a fixed downstream reconstruction project;
- transport host archives, BMI or PCM files to wasm32.

## Installed Protocol Packages

An installed protocol package contains its native archive, source/module inputs
and prefix-relative Forge metadata.

- A native consumer imports the installed archive.
- A guest consumer materializes the installed sources under the active wasm32
  toolchain using the same public target name.
- The package contains no absolute source/build paths and no compiler module
  artifacts.

Package materialization is generated from the explicit declaration. It does not
inspect the exported native CMake link graph.

## Graph And Attestation Invariants

- IDs and logical paths are unique inside one configuration.
- Every declared file belongs to its source root and exactly one role.
- Dependencies are contract libraries or explicitly guest-compatible Forge
  components.
- Public and private scopes are preserved by the configured CMake target.
- Dependency cycles and host-only guest dependencies fail at configure time.
- The guest descriptor includes every ABI and attestation input.
- Manifest schema v2 and its length-prefixed source digest remain canonical.
- Direct and launcher builds produce byte-identical WASM, ABI and manifest
  artifacts from identical sources and toolchains.

## Rejected Mechanisms

- host-generated guest graph JSON used as target-construction input;
- a fixed `guest/build` project that reconstructs downstream targets;
- reverse parsing or sealing of arbitrary native CMake target properties;
- central host-target-to-guest-target name mappings;
- CMake File API as a cross-toolchain protocol;
- custom module-name parsing or installed BMI transport;
- copying product sources into a generated guest tree.

Ubuntu and macOS CI confirm portability only after the direct project and
launcher paths pass the focused local ARM64 gate.
