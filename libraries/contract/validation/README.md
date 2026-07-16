# Contract Validation

Target `forge_contract_validation`, package component `contract_validation`,
validates a contract ABI, WebAssembly module, exports and approved imports.
Public modules are `forge.contract.validation.validator` and
`forge.contract.validation.command`.

```cpp
import forge.contract.validation.validator;

forge::contract::validation::validate({
   .wasm = "token.wasm",
   .abi = "token.abi",
   .imports = "intrinsics.json",
});
```

Validation uses `forge.vm.wasm` and `forge.chain.protocol`; it does not invoke
Clang and the package component does not pull Clang transitively. Unknown or
wrongly typed imports, WASI, malformed ABI/WASM and a missing `apply` export are
errors. The command adapter maps exceptions to process exit codes.

## Dependencies

`forge_chain_protocol`, `forge_codec_json` and `forge_vm_wasm` provide ABI
parsing and structural WASM validation. The library does not link or discover
Clang/LLVM.

## Stability And Tests

The validation request is experimental; accepted feature/import policy is a
versioned SDK contract. Tests cover valid contracts, malformed ABI and WASM,
unknown or mismatched imports, WASI, disabled features, missing exports,
standalone package consumption and the generated SDK artifacts.
