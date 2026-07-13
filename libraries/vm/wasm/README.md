# forge_vm_wasm

`forge_vm_wasm` is Forge's native WebAssembly virtual machine. It is a C++23
module port of AntelopeIO EOS VM and preserves the donor parser, validator,
interpreter, host-function ABI, guarded memory, watchdog, deterministic
SoftFloat operations, and x86_64 just-in-time compiler.

## Public Surface

- Target: `forge_vm_wasm`
- Package target: `Forge::forge_vm_wasm`
- Package component: `vm_wasm`
- Namespace: `forge::vm::wasm`
- Modules:
  - `forge.vm.wasm.allocator`
  - `forge.vm.wasm.backend`
  - `forge.vm.wasm.debug_info`
  - `forge.vm.wasm.exceptions`
  - `forge.vm.wasm.host_function`
  - `forge.vm.wasm.options`
  - `forge.vm.wasm.types`
  - `forge.vm.wasm.watchdog`

`forge::vm` is a family root. The library does not create a `forge_vm` target
or an aggregate `forge.vm.wasm` module.

## Backends

The interpreter is available on supported Unix platforms. The JIT backend is
available only on x86_64, matching the donor's native code generator. Forge
does not provide an ARM64 JIT fallback.

```cpp
import forge.vm.wasm.backend;

using engine = forge::vm::wasm::backend<
   std::nullptr_t,
   forge::vm::wasm::interpreter>;

auto vm = engine{};
```

Applications normally supply registered host functions and a
`forge::vm::wasm::wasm_allocator`, then construct a backend from canonical WASM
bytes. Public code never includes SoftFloat C headers or the installed internal
implementation assets directly.

## Boundaries

This library owns only neutral WASM mechanics. It does not own blockchain
controller behavior, contract policy, state storage, code cache policy,
resource billing, transaction execution, or product-specific host functions.
Those belong to consuming products.

`compatibility_options` preserves the donor validation profile without using a
product name. Selecting that profile remains a product decision.

## Tests

The test tree is generated from the pinned EOS VM donor suite and uses
Boost.Test. A manifest records donor source hashes, test cases, assertion
counts, backend matrices, and fixture hashes. The mapping gate rejects changes
outside the explicit namespace, symbol, exception, macro, and assertion-framework
translations.

CI also builds and executes the donor's original Catch2 test bodies as an
oracle. That pinned revision has two modern-toolchain build defects: its signal
header neither includes its allocator definitions nor accepts the null
allocators used by its own tests, and its host result conversion inspects
forwarding-reference types without removing references.
The oracle applies hash-checked compatibility patches to those two headers;
the donor test tree remains unchanged and is verified as such before it is
built.

`forge_vm_wasm_spec_test_generator` is an excluded-from-all test maintenance
tool copied from the pinned donor. `tests/vm_wasm/regenerate_spec_test.py` maps
its JSON output through the same checked Forge/Boost transformation. The
optional `FORGE_VM_WASM_ENABLE_FUZZ_TESTS=ON` target builds the parser harness
with Clang libFuzzer and AddressSanitizer; it is not installed.

- ARM64 runs the complete interpreter suite.
- x86_64 CI runs interpreter and JIT suites in Debug and Release.
- The package consumer verifies a relocatable
  `find_package(Forge COMPONENTS vm_wasm)` installation.

See `PROVENANCE.md`, `THIRD_PARTY_LICENSES`, and `LICENSE.eos-vm` for source
lineage and licensing.
