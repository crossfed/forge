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

The generated dispatcher includes the canonical first source path, so quoted
includes keep normal C++ lookup semantics. Integer ABI names are selected from
the Clang target model; in wasm32, `long` is 32 bits. The library contains no
CLI `main`, compiler patches or guest runtime. Its tests are the pinned CDT
pass/fail fixtures plus local-include and wasm32-width regressions.

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
