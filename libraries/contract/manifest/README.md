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
   .source_graph = "token.contract-graph.json",
   .output = "token.contract.json",
   .sdk_version = "8.5.0",
   .profile = "release",
   .reproducible = true,
});
```

The manifest records artifact hashes, imported functions, enabled WASM
features, the canonical source graph and toolchain identity without modifying WASM bytes. Reproducible
release profiles record the pinned LLVM tag and commit. Developer profiles
record the selected Clang version line and omit the unknown source commit.
This library does not require Clang and does not validate policy; use
`contract_validation` first.

## Dependencies

`forge_codec_json`, `forge_crypto_digest` and `forge_vm_wasm` provide
deterministic JSON, SHA-256 and WASM feature/import inspection. No Clang
dependency is transitive through package component `contract_manifest`.

## Stability And Tests

The generator request is Experimental in Forge 8.16.0; manifest schema v2 and
deterministic field meaning are Stable, versioned compatibility surfaces. Tests
verify hashes, import and feature capture, source ownership and scoped
dependency digests, reproducible output, command failures, standalone package
consumption and relocated SDK generation.

Schema v2 is the intentional Forge 8.16 migration from schema v1. Existing
schema-v1 sidecars are generated build artifacts and must be regenerated with
the Forge 8.16 Contract SDK. Consumers that validate `schema_version` must add
schema v2 before upgrading; v2 adds the attested `source_graph` and does not
offer a v1 output mode. The Forge 8.16 release note repeats this migration when
the release version is cut.

The source graph attests its root owner, logical file identities and hashes,
scoped dependency edges, and canonical component-to-module ownership. Absolute
source roots and physical paths are transport details and are excluded so the
same installed package produces the same digest after relocation.
