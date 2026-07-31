# Contract Host Libraries

`libraries/contract` is an empty family for host-side contract build services.
It deliberately has no `forge_contract` target and no `forge.contract` host
module. Consumers select only the capability they need:

| Library | Package component | Purpose |
|---|---|---|
| `abi` | `contract_abi` | Clang AST to chain ABI and dispatcher |
| `attributes` | `contract_attributes` | Clang attribute registration |
| `validation` | `contract_validation` | ABI and WebAssembly validation |
| `manifest` | `contract_manifest` | Deterministic build manifests |
| `testing` | `contract_testing` | Deterministic VM and ObjectDB contract test host |

Build these optional libraries with `FORGE_ENABLE_CONTRACT_TOOLING=ON`.
Only `abi` and `attributes` require a compatible Clang/LLVM package. Contract
guest code, the wasm32 sysroot and command-line programs live in `guest/` and
`tools/` respectively.

`testing` executes contract WASM against deterministic authorization and
ObjectDB state. It is an SDK test fixture, not a controller, consensus runtime,
or production blockchain host.
