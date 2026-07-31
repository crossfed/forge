# Contract Manifest

Target `forge_contract_manifest`, package component `contract_manifest`, writes
the deterministic `*.contract.json` runtime sidecar. Public modules are
`forge.contract.manifest.generator` and
`forge.contract.manifest.command`.

```cpp
import forge.contract.manifest.generator;

forge::contract::manifest::generate({
   .wasm = "token.wasm",
   .abi = "token.abi",
   .imports = "intrinsics.json",
   .output = "token.contract.json",
   .sdk_version = "8.19.0",
   .profile = "release",
   .reproducible = true,
});
```

Schema v3 records artifact hashes, imported functions, enabled WASM features
and pinned toolchain identity. It does not claim that deployed bytes correspond
to a published source tree.

Source verification is a separate future attestation layer. It will require a
hermetic build and compiler-derived dependencies rather than a second
declarative CMake graph.

`forge_codec_json`, `forge_crypto_digest` and `forge_vm_wasm` provide
deterministic JSON, SHA-256 and WASM inspection. The library does not require
Clang and does not validate policy; use `contract_validation` first.

The generator request and manifest schema are Experimental. Schema v2 sidecars
with `source_graph` are generated artifacts and must be regenerated as schema
v3; no compatibility output mode is provided.
