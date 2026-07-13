module;

#include <details/prelude.hxx>
#include <details/opcodes_def.hxx>

#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#include <ucontext.h>

export module forge.vm.wasm.backend;

export import forge.vm.wasm.allocator;
export import forge.vm.wasm.debug_info;
export import forge.vm.wasm.exceptions;
export import forge.vm.wasm.host_function;
export import forge.vm.wasm.options;
export import forge.vm.wasm.types;
export import forge.vm.wasm.watchdog;

#include <details/bitcode_writer.hxx>
#include <details/config.hxx>
#include <details/debug_visitor.hxx>
#include <details/execution_context.hxx>
#include <details/interpret_visitor.hxx>
#include <details/null_writer.hxx>
#include <details/parser.hxx>

#if defined(__x86_64__)
#include <details/x86_64.hxx>
#endif

#undef FORGE_VM_WASM_EXPORT
#define FORGE_VM_WASM_EXPORT export
#include <details/backend.hxx>
#undef FORGE_VM_WASM_EXPORT
