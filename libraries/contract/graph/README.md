# Contract Graph

Target `forge_contract_graph` owns the validated, in-memory view of a Forge
Contract SDK source graph. Its public module is `forge.contract.graph` in
namespace `forge::contract::graph`.

Use this library in host tooling that consumes the versioned graph emitted by
`forge_add_contract`. The descriptor records source owners, file roles,
portable logical paths and explicit public/private dependency edges.

```cpp
import forge.contract.graph;

const auto graph = forge::contract::graph::read("token.contract-graph.json");
for (const auto& file : graph.files) {
   if (forge::contract::graph::is_public(file.role)) {
      // Expose only the declared public protocol inputs.
   }
}
```

`read()` rejects unsupported schemas, duplicate owners or paths, unknown
dependencies, dependency cycles, invalid file roles and files outside their
declared source roots. Unreadable inputs throw
`forge::contract::graph::exceptions::read_error`; malformed descriptors throw
`forge::contract::graph::exceptions::invalid_descriptor`. Both include the
input path in structured Forge exception context.

## Dependencies

`forge_codec_json` provides canonical JSON decoding and `forge_exceptions`
provides the typed public error boundary. The library has no CMake, Clang,
ABI-generation or WASM-runtime dependency.

## Tests And Boundaries

`test_forge_contract` covers valid descriptors, missing component IDs,
duplicate module ownership and library/component cycles. Installed-package and
relocation fixtures cover the descriptor produced from downstream
dual-target libraries.

This library validates an explicit Forge-owned descriptor. It does not inspect
native CMake link properties, discover undeclared files, hash sources or infer
host-to-guest target mappings. Source attestation belongs to
`forge_contract_manifest`; graph production and dependency visibility belong
to the Contract SDK CMake API.
