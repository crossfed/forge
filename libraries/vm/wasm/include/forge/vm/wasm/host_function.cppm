module;

#include <details/prelude.hxx>

export module forge.vm.wasm.host_function;

export import forge.vm.wasm.types;

#include <details/function_traits.hxx>

#undef FORGE_VM_WASM_EXPORT
#define FORGE_VM_WASM_EXPORT export
#include <details/argument_proxy.hxx>
#include <details/host_function.hxx>
#undef FORGE_VM_WASM_EXPORT
