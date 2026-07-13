module;

#include <details/prelude.hxx>

export module forge.vm.wasm.watchdog;

#undef FORGE_VM_WASM_EXPORT
#define FORGE_VM_WASM_EXPORT export
#include <details/watchdog_detail.hxx>
#undef FORGE_VM_WASM_EXPORT
