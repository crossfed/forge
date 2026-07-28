# Forge Contract Dual-Target Graph Donor Baseline v1

This note defines the build-graph model for Forge contract protocol libraries.
A contract protocol library is one logical package with two configured
materializations: a native host library and a wasm32 guest library. The Forge
descriptor, not CMake target internals, is the protocol exchanged between those
materializations.

## Accepted Donors

### CDT

Pinned CDT commit `69599db279b7b93d0688502720c15c6962a1401b`
defines the accepted guest compilation, ABI generation and dispatcher
semantics. In particular, an action is compiled for wasm32, decoded from the
incoming action payload and dispatched to contract code.

CDT is not a dual-target package-graph donor. Its `add_contract` and
`add_native_library` entry points create separate targets and do not preserve
one installable host/guest dependency graph.

### Bazel

Bazel configured targets and providers supply the graph-model donor:

- a logical target is distinct from one configured target;
- dependencies are typed edges rather than strings recovered from linker state;
- analysis produces immutable metadata before execution;
- consumers receive only declared public providers.

Forge adopts those invariants without importing Bazel, Starlark or Bazel
providers. A Forge contract-library descriptor is the typed analysis result.

### Cargo

Cargo host and target build units supply the materialization donor. One package
can be compiled independently for the host and target triples while preserving
the same package identity and explicit dependency graph.

Forge adopts the separation between logical identity and configured build unit.
It does not import Cargo metadata or Rust build logic.

### CMake

CMake remains the implementation substrate:

- `ExternalProject` isolates the wasm32 build;
- `FILE_SET CXX_MODULES` owns module inputs;
- `install(EXPORT ... CXX_MODULES_DIRECTORY ...)` exports host module metadata;
- package configuration supports relocated downstream consumers.

CMake continues to own each native C++ graph. Forge does not read
`LINK_LIBRARIES`, `INTERFACE_LINK_LIBRARIES`, generator-expression wrappers or
directory-internal target encodings to construct a second graph.

## Canonical Descriptor

`forge_add_contract_library` creates one immutable descriptor containing:

- descriptor schema version and stable library ID;
- source root and module base directories;
- files with explicit roles and logical paths;
- public and private dependency IDs;
- the concrete native target and immutable public alias.

`forge_add_contract` creates the contract-root descriptor with explicit
sources, headers, compile checks and contract-library dependencies.

The descriptor is normalized once. Forge derives five views from that value:

1. native host compilation;
2. isolated wasm32 compilation;
3. installed source package;
4. ABI generator input;
5. source-graph attestation.

These views may filter the descriptor by role or edge scope. They must not
rediscover dependencies from generated build state.

## Graph Invariants

- IDs are globally unique inside one configure.
- File logical paths are unique inside one descriptor.
- Every file is inside its declared source root.
- Contract-library dependencies name Forge descriptors or guest-compatible
  Forge components with stable IDs.
- Public edges are visible to downstream libraries and contracts.
- Private edges are visible only while compiling the owner implementation.
- The descriptor graph is acyclic.
- An installed descriptor contains prefix-relative paths only.
- Installed packages contain source and module inputs, never BMI or PCM files.
- The guest build receives versioned JSON plus its SHA-256 and rejects any
  schema or digest mismatch.

Native CMake mutation is intentionally unavailable through the public alias.
All files, dependencies and supported options must be declared in the Forge
call. This makes incomplete descriptors configure errors instead of
platform-dependent build failures.

## Rejected Mechanisms

- reverse parsing of native CMake target properties;
- parsing `$<LINK_ONLY:...>` or CMake `::@(...)` directory wrappers;
- central host-target-to-guest-target name switches;
- CMake File API as a cross-toolchain protocol;
- custom C++ module-name parsers or installed BMI transport;
- importing Bazel, Cargo or CDT build systems into Forge;
- dependency ownership in the Clang attribute plugin.

## Validation Consequences

The local ARM64 gate exercises the normalized graph, not individual fixes. Its
table includes direct and transitive public/private edges, distinct Forge crypto
leaves, invalid IDs, cycles, undeclared inputs, build-tree consumption and
relocated install-tree consumption. The same descriptor drives ABI, VM and
attestation fixtures.

Ubuntu and macOS CI are portability confirmation after a clean review, not the
primary graph debugger.
