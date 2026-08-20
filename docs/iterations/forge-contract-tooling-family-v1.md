# Forge Contract Tooling Family v1

## Status

Accepted direction. Implementation is a clean pre-stable source and package
break and is deferred to the corresponding Forge cleanup block.

## Problem

The current host-side `libraries/contract` family contains developer and build
tools rather than contract-domain values:

- Clang AST ABI and dispatcher generation;
- Clang attribute registration;
- ABI and WebAssembly validation;
- deterministic build manifests;
- deterministic VM and ObjectDB contract testing.

Naming these libraries `forge.contract.*` conflates host tooling with the
guest-side Contract API, whose actual domain is `forge::contract`. It also makes
the package graph look as though host applications should depend on contract
runtime types when they only need a compiler, validator, manifest generator or
test host.

## Accepted Identity

The host family moves from `libraries/contract` to the empty family root
`libraries/tooling`:

| Current | New |
| --- | --- |
| `libraries/contract/abi` | `libraries/tooling/abi` |
| `forge_contract_abi` | `forge_tooling_abi` |
| `Forge::forge_contract_abi` | `Forge::forge_tooling_abi` |
| `contract_abi` component | `tooling_abi` component |
| `forge.contract.abi.*` | `forge.tooling.abi.*` |
| `forge::contract::abi` | `forge::tooling::abi` |
| `include/forge/contract/abi` | `include/forge/tooling/abi` |
| `libraries/contract/attributes` | `libraries/tooling/attributes` |
| `forge_contract_attributes` | `forge_tooling_attributes` |
| `Forge::forge_contract_attributes` | `Forge::forge_tooling_attributes` |
| `contract_attributes` component | `tooling_attributes` component |
| `forge.contract.attributes.*` | `forge.tooling.attributes.*` |
| `forge::contract::attributes` | `forge::tooling::attributes` |
| `include/forge/contract/attributes` | `include/forge/tooling/attributes` |
| `libraries/contract/validation` | `libraries/tooling/validation` |
| `forge_contract_validation` | `forge_tooling_validation` |
| `Forge::forge_contract_validation` | `Forge::forge_tooling_validation` |
| `contract_validation` component | `tooling_validation` component |
| `forge.contract.validation.*` | `forge.tooling.validation.*` |
| `forge::contract::validation` | `forge::tooling::validation` |
| `include/forge/contract/validation` | `include/forge/tooling/validation` |
| `libraries/contract/manifest` | `libraries/tooling/manifest` |
| `forge_contract_manifest` | `forge_tooling_manifest` |
| `Forge::forge_contract_manifest` | `Forge::forge_tooling_manifest` |
| `contract_manifest` component | `tooling_manifest` component |
| `forge.contract.manifest.*` | `forge.tooling.manifest.*` |
| `forge::contract::manifest` | `forge::tooling::manifest` |
| `include/forge/contract/manifest` | `include/forge/tooling/manifest` |
| `libraries/contract/testing` | `libraries/tooling/testing` |
| `forge_contract_testing` | `forge_tooling_testing` |
| `Forge::forge_contract_testing` | `Forge::forge_tooling_testing` |
| `contract_testing` component | `tooling_testing` component |
| `forge.contract.testing.*` | `forge.tooling.testing.*` |
| `forge::contract::testing` | `forge::tooling::testing` |
| `include/forge/contract/testing` | `include/forge/tooling/testing` |
| `FORGE_ENABLE_CONTRACT_TOOLING` | `FORGE_ENABLE_TOOLING` |

`libraries/tooling` remains an empty family root. It does not create a
`forge_tooling` target, a `tooling` package component or an aggregate
`forge.tooling` module. Consumers depend on the smallest leaf capability.

## Contract Boundary

The rename applies only to host-side developer, build and test tooling. The
following contract-domain identities remain unchanged:

- `guest/libraries/contract` and namespace `forge::contract`;
- guest value types, intrinsics and serialization contracts;
- `[[forge::contract]]` and other contract source attributes;
- the Forge Contract SDK product name and package role;
- contract-specific CMake operations such as `forge_add_contract`;
- contract ABI, dispatcher, manifest and WebAssembly output formats;
- donor-compatible EOSIO contract veneers and unchanged donor source corpus.

Host tooling may consume guest contract descriptions, but guest code must not
depend on `forge.tooling.*`. Chain protocol and runtime contract execution also
do not move into the tooling family.

## Migration Policy

This is a clean break. Compatibility targets, forwarding modules, namespace
aliases and duplicate package components are forbidden. All Forge-owned
consumers, package tests, component registries, examples and documentation move
in the same change.

Public exception category identities owned by these libraries move from
`forge.contract.<leaf>` to `forge.tooling.<leaf>`. Contract-domain diagnostics
emitted by guest code remain under their existing contract identity.

Generated ABI, WebAssembly, dispatcher and manifest bytes must remain
unchanged for identical inputs. Only source, target, module, namespace and
package-component identity changes.

## Implementation Order

1. Move the five leaf libraries under `libraries/tooling` and update the root
   build graph.
2. Rename targets, exported CMake targets, package components, modules and
   namespaces together.
3. Update tools and Contract SDK integration to consume the new leaf targets.
4. Rename focused tests and package relocation consumers.
5. Update library READMEs and structural gates, then remove all stale host-side
   `forge.contract.*` identities.

## Acceptance

- no host target named `forge_contract_abi`, `forge_contract_attributes`,
  `forge_contract_validation`, `forge_contract_manifest` or
  `forge_contract_testing` remains;
- no host module or namespace remains under `forge.contract.abi`,
  `forge.contract.attributes`, `forge.contract.validation`,
  `forge.contract.manifest` or `forge.contract.testing`;
- no host public include path or exception category remains under the old
  `forge/contract/<leaf>` or `forge.contract.<leaf>` identity;
- guest `forge::contract` consumers compile unchanged;
- ABI, dispatcher, validation, manifest and Contract Testing behavior remains
  byte-for-byte or semantically identical as applicable;
- each new `tooling_*` package component installs and relocates independently;
- Contract SDK foundation and unchanged-source EOSIO consumers remain green;
- structure gates reject aggregate tooling targets/modules and compatibility
  aliases;
- `git diff --check`, format checks and the focused host/guest test suites pass.

## Non-goals

- redesigning the Contract SDK or guest API;
- changing contract wire or ABI formats;
- renaming contract attributes or contract-oriented build commands merely
  because their implementation uses tooling;
- introducing a generic compiler framework unrelated to Forge contracts;
- implementing the separate `forge.vm.wasm.interpret` backend-family migration.
