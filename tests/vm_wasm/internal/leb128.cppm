module;

#include <details/prelude.hxx>

export module forge.vm.wasm.tests.leb128;

export import forge.vm.wasm.types;

#undef FORGE_VM_WASM_EXPORT
#define FORGE_VM_WASM_EXPORT export
#include <details/leb128.hxx>
#undef FORGE_VM_WASM_EXPORT
