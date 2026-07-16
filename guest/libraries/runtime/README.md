# Guest Runtime

`forge_guest_runtime` is the freestanding wasm32 runtime used by Forge
contracts. It supplies the CDT-derived allocator, C memory/string primitives,
global new/delete, guest-local `errno`, libc++ abort glue and `memory.grow`
integration.

The allocator supports aligned allocation, reuse, coalescing, `calloc` and
`realloc`. Allocation failures terminate through the canonical contract check
intrinsic. It owns no files, sockets, threads, clocks or host heap. Private
allocator mechanics are paired under `details/*.hxx`; there is no fake public
module because the external C ABI comes from generated sysroot headers.

Allocator fragmentation, alignment, reuse, growth and exhaustion are exercised
by guest contracts and then executed through `forge.vm.wasm`. Oversized
requests and arithmetic overflow fail without changing existing allocations.
Aligned allocations are identified by allocator-owned block metadata; bytes in
ordinary allocation headers cannot redirect `free` or `realloc`.

## Dependencies And Boundary

The target uses only the wasm32 compiler builtins, generated intrinsic C API
and the configured guest sysroot. It has no C++ module because its public ABI is
the standard C/runtime surface consumed by libc++ and contract code.

## Stability And Tests

Allocation and C memory semantics are production compatibility contracts;
private block layout is not API. Tests cover fragmentation, reuse, aligned
allocation, split/coalesced blocks, growable memory, `calloc`, `realloc`,
new/delete and deterministic exhaustion through the real VM memory export.
The E2E suite also links and executes `<cerrno>` reads and writes, proving that
`errno` does not escape as an undeclared host import.
