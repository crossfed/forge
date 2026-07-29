---
name: create-contract-library
description: Use when creating, extending, refactoring, or reviewing a dual-target contract protocol library built for both the Forge host SDK and a WASM guest contract.
---

# Skill: Dual-Target Contract Library

## Prerequisite

Apply `skills/create-library/SKILL.md` first. A contract library follows the
same `.cppm`, `.cpp`, `.hxx`, naming, pairing, and include-path rules as every
other Forge library. This skill adds the cross-toolchain contract constraints;
it does not replace the library layout skill.

This skill applies only to dual-target protocol libraries declared with
`forge_add_contract_library`. It does not apply to host-only libraries such as
`libraries/contract/testing`.

## Declaration

Declare the complete source and dependency graph once:

```cmake
forge_add_contract_library(
   product_protocol
   ID product.chain.protocol
   SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
   MODULE_BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
   MODULE_SOURCES
      include/product/chain/protocol/value.cppm
   SOURCES
      value.cpp
      value_impl.cpp
   PUBLIC_HEADERS
      include/product/chain/protocol/macros.hpp
   PRIVATE_HEADERS
      details/value_impl.hxx
   PUBLIC_LIBRARIES
      Forge::forge_chain_protocol
      Forge::forge_raw
   PRIVATE_LIBRARIES
      other_contract_library
)
```

The corresponding private implementation is `value_impl.cpp`, paired with
`details/value_impl.hxx`; `value.cpp` is paired only with `value.cppm`.

- `ID` is the stable package and guest-component identity.
- Every file belongs to `SOURCE_ROOT`, has one logical path, and has one role.
- Every dependency is declared as `PUBLIC_LIBRARIES` or `PRIVATE_LIBRARIES`.
- Public dependencies are available to downstream protocol consumers.
- Private dependencies are available only to implementation units owned by the
  declaring library.
- Dependencies must expose a guest-compatible Forge component ID or be another
  contract library.
- Host-only dependencies, dependency cycles, duplicate IDs, duplicate logical
  paths, and undeclared inputs are errors.

Do not mutate the target later with native `target_sources`,
`target_link_libraries`, or compile/link option commands. Do not inspect
`LINK_LIBRARIES`, `INTERFACE_LINK_LIBRARIES`, `LINK_ONLY`, generated directory
wrappers, or target-name conventions to reconstruct a guest graph.

## Protocol Surface

- Shared strong IDs, enums, action payloads, table values, index keys, and pure
  checked calculations live in the contract library.
- An action payload owns its external name through a public
  `static constexpr get_name()` returning
  `forge::chain::protocol::action_name`.
- A named payload is packed by the host with `forge::raw` and unpacked directly
  by guest dispatch. Do not add a wrapper object such as `{ value: payload }`.
- Table/action names and persisted values have one host/guest definition.
- Authorization, `multi_index` or `singleton` declarations, state mutations,
  and dispatch remain guest-only.
- RPC, signing orchestration, caches, retries, and external JSON boundaries
  remain host-only.

## Packaging

- Install sources, module interfaces, public headers, and versioned graph
  metadata required to rebuild the library for another toolchain.
- Export an installable protocol target through
  `forge_install_contract_library`.
- Never install BMI or PCM files.
- Installed metadata must be relocatable and contain no source/build absolute
  paths.
- Consumers pass the installed target directly to `forge_add_contract`.

## Required Validation

1. Build and import the protocol library on the host.
2. Build the same declared graph for the WASM guest.
3. Prove identical `forge::raw` bytes for representative shared values.
4. Prove named-action ABI fields are direct and dispatch receives the same DTO.
5. Exercise persisted state through `Forge::forge_contract_testing`.
6. Install, relocate, and consume the package from an independent CMake project.
7. Verify the contract manifest contains the deterministic declared source
   graph and no absolute paths.

The SDK is the owner of graph materialization. Product projects must not create
compatibility modules, copy guest runtimes, invent a second serializer, or
reimplement the contract test host.
