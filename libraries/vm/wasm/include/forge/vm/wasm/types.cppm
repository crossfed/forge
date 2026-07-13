module;

#include <details/prelude.hxx>

export module forge.vm.wasm.types;

export import forge.vm.wasm.allocator;
export import forge.vm.wasm.exceptions;

#undef FORGE_VM_WASM_EXPORT
#define FORGE_VM_WASM_EXPORT export
#include <details/variant.hxx>
#include <details/opcodes.hxx>
#include <details/vector.hxx>
#include <details/utils.hxx>
#include <details/guarded_ptr.hxx>
#include <details/stack_elem.hxx>
#include <details/types.hxx>
#include <details/wasm_stack.hxx>
#include <details/execution_interface.hxx>
#undef FORGE_VM_WASM_EXPORT
