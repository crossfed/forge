# Contract Manifest

Target `forge_contract_manifest`, package component `contract_manifest`, writes
the deterministic `*.contract.json` sidecar. Public modules are
`forge.contract.manifest.generator` and `forge.contract.manifest.command`.

```cpp
import forge.contract.manifest.generator;

forge::contract::manifest::generate({
   .wasm = "token.wasm",
   .abi = "token.abi",
   .imports = "intrinsics.json",
   .output = "token.contract.json",
   .sdk_version = "8.5.0",
   .profile = "release",
   .reproducible = true,
});
```

The manifest records artifact hashes, imported functions, enabled WASM
features and toolchain identity without modifying WASM bytes. Reproducible
release profiles record the pinned LLVM tag and commit. Developer profiles
record the selected Clang version line and omit the unknown source commit.
This library does not require Clang and does not validate policy; use
`contract_validation` first.

## Dependencies

`forge_codec_json`, `forge_crypto_digest` and `forge_vm_wasm` provide
deterministic JSON, SHA-256 and WASM feature/import inspection. No Clang
dependency is transitive through package component `contract_manifest`.

## Stability And Tests

The generator request is experimental; manifest schema and deterministic field
meaning are versioned compatibility surfaces. Tests verify hashes, import and
feature capture, reproducible output, command failures, standalone package
consumption and relocated SDK generation.
