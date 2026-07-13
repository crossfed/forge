module;

#include <details/prelude.hxx>

export module forge.vm.wasm.tests.signals;

export import forge.vm.wasm.allocator;
export import forge.vm.wasm.exceptions;
export import forge.vm.wasm.types;

#undef FORGE_VM_WASM_EXPORT
#define FORGE_VM_WASM_EXPORT export
#include <details/signals.hxx>
#undef FORGE_VM_WASM_EXPORT
