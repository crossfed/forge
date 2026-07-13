module;

#include <details/prelude.hxx>

export module forge.vm.wasm.allocator;

export import forge.vm.wasm.exceptions;

#undef FORGE_VM_WASM_EXPORT
#define FORGE_VM_WASM_EXPORT export
#include <details/scope_guard.hxx>
#include <details/constants.hxx>
#include <details/span.hxx>
#include <details/allocator.hxx>
#undef FORGE_VM_WASM_EXPORT
