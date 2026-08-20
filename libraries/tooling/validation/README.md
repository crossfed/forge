# Contract Validation

Target `forge_tooling_validation`, package component `tooling_validation`,
validates a contract ABI, WebAssembly module, exports and approved imports.
Public modules are `forge.tooling.validation.validator` and
`forge.tooling.validation.command`.

```cpp
import forge.tooling.validation.validator;

forge::tooling::validation::validate({
   .wasm = "token.wasm",
   .abi = "token.abi",
   .imports = "intrinsics.json",
});
```

Validation uses `forge.vm.wasm.interpret` and `forge.chain.protocol`; it does not invoke
Clang and the package component does not pull Clang transitively. Unknown or
wrongly typed imports, WASI, malformed ABI/WASM, a missing `apply` export and an
`apply` export other than `(i64, i64, i64) -> void` are errors. The command
adapter maps exceptions to process exit codes.

## Dependencies

`forge_chain_protocol`, `forge_codec_json` and `forge_vm_wasm_interpret` provide ABI
parsing and structural WASM validation. The library does not link or discover
Clang/LLVM.

## Stability And Tests

The validation request is Experimental in Forge 8.16.0; accepted feature/import
policy is a Stable, versioned SDK contract. Tests cover valid contracts,
malformed ABI and WASM, unknown or mismatched imports, WASI, disabled features,
missing exports, wrongly typed `apply`, standalone package consumption and the
generated SDK artifacts.
