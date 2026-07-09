# FCL Aggressive Core Prune v1

## Summary

This pass makes `fcl_core` a real low-level base instead of a bucket for old FC surfaces. Source-level compatibility with old `fc::...` APIs is not retained. The compatibility boundary remains binary: `fcl::raw::pack` and `fcl::raw::unpack` must keep the old FC byte layout for retained types.

## Core Boundary

`fcl_core` may contain only foundational value helpers that do not depend on higher domains:

- bit and byte helpers;
- string and UTF-8 helpers;
- `std::chrono` formatting and old FC wire helper functions;
- `uint128`;
- version/build metadata.

`fcl_core` must not import `fcl.crypto`, `fcl.raw`, `fcl.variant`, `fcl.json` or `fcl.log`.

## Removed Source APIs

The following old FC-like public APIs are intentionally removed:

- `fcl::filesystem`;
- `fcl::mock_time`;
- `fcl::mutex`;
- `fcl::array`;
- `fcl::fixed_string`;
- `fcl::safe`;
- `fcl::fwd`;
- `fcl::scoped_exit`;
- core `byteswap` and `bitutil` modules;
- flat/interprocess container helpers.

If a consumer needs filesystem helpers, it should use `std::filesystem` directly or define application-specific helpers in the consuming project. FCL core is not an application configuration or path-layout library.

If a domain needs array, forwarding or safety wrappers internally, it must use standard C++ ownership (`std::array`, `std::unique_ptr`, concrete private structs) or keep implementation-only helpers inside that domain. These helpers must not return to `core` as public compatibility types.

## Raw, Variant And JSON

Raw serialization lives only in `libraries/raw`. `core` must not define `namespace fcl::raw` or raw overloads.

Variant and JSON glue for retained scalar/value types lives in `libraries/variant` and `libraries/codec/json`, not in the foundational core modules. This keeps the dependency graph one-way: higher domains may adapt core values, but core does not know about higher domains.

## Time

The old FC-style time source API is removed. Public time values use `std::chrono`; `fcl_core` keeps only neutral formatting/parsing and old FC wire conversion helpers. Tests that need deterministic time must pass explicit values.

## Crypto

Crypto primitives remain in scope for FCL and are not pruned in this pass. The crypto domain was updated away from removed compatibility helpers:

- `fcl::array` becomes `std::array`;
- `fcl::fwd` becomes `std::unique_ptr<impl>` or concrete private ownership;
- behavior of keys, signatures, hashes, AES and encoding helpers is preserved by existing crypto tests.

## Compatibility Acceptance

Raw byte compatibility is required for retained types:

- primitives;
- strings and containers;
- optionals and maps;
- enums;
- Boost.Describe structs/classes;
- `static_variant` and `variant`;
- crypto key and signature wrappers.

No raw compatibility is required for public types removed from FCL.

## Static Gates

The cleanup is guarded by:

```sh
rg "import fcl\\.(crypto|raw|variant|json|log)" libraries/core
rg "namespace fcl::raw|namespace raw" libraries/core
rg "fcl\\.core\\.(filesystem|mock_time|mutex|array|fixed_string|safe|fwd|interprocess_container|container_flat|container_container_detail)" libraries tests
rg "<old-chain-comment-markers>" libraries tests docs AGENTS.md
rg "<old-reflection-macro-or-reflector-markers>" libraries tests docs AGENTS.md
```

All commands must return no matches.

## Non-goals

- No crypto pruning.
- No Boost.Describe schema/validation layer.
- No new filesystem/config helper layer.
- No source-level FC compatibility.
- No Storlane superproject pointer update.
