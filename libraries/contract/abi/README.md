# Contract ABI

Target `forge_contract_abi`, package component `contract_abi`, owns Clang AST
analysis, Spring/CDT-compatible ABI generation, Ricardian metadata and generated
dispatchers. Public modules are `forge.contract.abi.generator` and
`forge.contract.abi.command` in namespace `forge::contract::abi`.

```cpp
import forge.contract.abi.generator;

auto result = forge::contract::abi::generate({
   .contract = "token",
   .abi = "token.abi",
   .dispatcher = "token.dispatcher.cpp",
   .attribute_plugin = "attr-plugin",
   .sysroot = "wasm32-sysroot",
   .sources = {"token.cpp"},
});
```

The first request source is the dispatch source and must declare the selected
contract. The generator verifies this rule and includes its canonical path, so
quoted includes keep normal C++ lookup semantics. `forge_add_contract` selects
the only source automatically; multi-source contracts must name
`DISPATCH_SOURCE` explicitly. When `request::depfile` is set, the generator
writes compiler-derived dependencies for every source/header opened during
analysis; Contract SDK CMake combines them with direct Ricardian dependencies
to prevent stale incremental ABI output. Integer ABI names are selected from
the Clang target model; in wasm32, `long` is 32 bits. Following CDT
`EOSIO_DISPATCH`, generated local actions execute only when `code == receiver`;
notifications require an explicit notification dispatcher. The library contains
no CLI `main`, compiler patches or guest runtime. Its tests are the pinned CDT
pass/fail fixtures plus local-include and wasm32-width regressions.

Modern `[[forge::action]]` and `[[forge::call]]` parameters must be named so
their ABI fields are usable by clients. The EOSIO spelling preserves CDT's
legacy unnamed-parameter output for source and ABI compatibility.

## Dependencies

- `forge_chain_protocol` and `forge_codec_json` for canonical ABI values;
- compatible `clang-cpp` and `LLVM` for target-aware AST analysis.

Requesting package component `contract_abi` is the only case where this API
causes Forge package discovery to require Clang.

## Stability And Tests

The request/artifact API is experimental until the first Contract SDK release;
the generated ABI and dispatcher behavior are compatibility contracts. Unit,
CDT fixture, quoted-include, wasm32 integer-width, action-result, package and
relocation tests cover the public surface.
