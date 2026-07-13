module;

#include <details/prelude.hxx>

export module forge.vm.wasm.debug_info;

#undef FORGE_VM_WASM_EXPORT
#define FORGE_VM_WASM_EXPORT export
#include <details/debug_info.hxx>
#undef FORGE_VM_WASM_EXPORT
