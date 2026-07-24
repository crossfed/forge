# Forge Contract Dual-Target Libraries V1

## Status

The API is experimental and targets the first stable Forge Contract SDK
release. It defines how downstream products share protocol values between host
code and WebAssembly contracts without generated compatibility types.

## Boundary

`forge_add_contract_library` owns one declarative source graph:

- a stable library `ID`;
- one `SOURCE_ROOT`;
- C++ module base directories and module interfaces;
- implementation files;
- public and private textual headers;
- guest-compatible Forge dependencies and contract-library dependencies.

The declaration creates a normal host static library. `forge_add_contract`
consumes metadata from that target and rebuilds the same sources in its isolated
wasm32 project. The guest graph is freestanding: DB, Asio, filesystem, plugins
and host-only crypto targets are rejected at configure time.

Forge does not parse module declarations. CMake owns module dependency
discovery, and Clang consumes build-local prebuilt-module directories. A
package transports source and metadata, never BMI or PCM compiler artifacts.

## Shared Surface

The intended shared layer contains:

- strong IDs and enums;
- action payloads;
- table row values and pure index-key calculations;
- canonical action and table names;
- chain protocol values;
- checked deterministic calculations;
- Raw serialization shape.

Guest-only code owns `multi_index`, authorization, state mutation and dispatch.
Host-only code owns RPC, transaction signing, retries, caches and service APIs.

An action payload with
`static constexpr forge::chain::protocol::action_name get_name()` is a named
DTO. A single named DTO parameter becomes the direct ABI action payload. Host
code packs the DTO with `forge::raw`; generated dispatch unpacks the same type.
An explicit action attribute cannot override the DTO name.

## Package Contract

`forge_install_contract_library` installs:

- the host archive through an ordinary CMake export;
- module interfaces and public headers under a module root;
- implementation and private headers under a source root;
- prefix-relative metadata attached to the imported target.

The downstream package config calls `find_dependency(Forge)` and
`find_dependency(ForgeContract)`. Relocating the complete install prefix must
not change host linking or guest contract compilation. Absolute source/build
paths and compiler module artifacts are forbidden in package metadata.

## Source Attestation

Contract manifest schema v2 contains a canonical `source_graph`. File records
are `{owner, role, logical_path, sha256}` and dependency records are
`{owner, dependency}`. Records are sorted before hashing.

The graph SHA-256 is calculated over an explicit domain string, record counts
and length-prefixed fields. Physical paths are used only to read source bytes;
they are never serialized. Contract sources, compile checks, Ricardian inputs,
contract-library modules, implementations and public/private headers are all
attested. `forge_add_contract` uses its call-site directory as the default
contract `SOURCE_ROOT`; `abigen` contributes every compiler-discovered include
under that root and explicitly declared include roots. The manifest skips
physical files already owned by the declarative library graph, so each source
has one attestation owner. Manifest v1 artifacts are rebuilt rather than
accepted as a compatibility format.

## Executable Proof

The independent `guest/tests/dual_target` fixture verifies:

- exact host Raw bytes for a shared action;
- inferred host action naming;
- direct named-action ABI layout;
- guest use of the shared row in `multi_index`;
- Forge VM execution and host Raw decoding of the stored row;
- package install, relocation and downstream `find_package`;
- guest compilation from an installed `Product::protocol` target;
- source graph roles, edges and deterministic content hashing;
- compiler-discovered local includes and digest changes after header edits;
- configure failures for invalid source and dependency graphs.

Existing VM E2E and Spring/EOSIO compatibility corpus remain mandatory. The
fixture proves the new downstream mechanism and does not replace donor
compatibility tests.
