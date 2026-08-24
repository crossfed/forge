module;

export module forge.vm.wasm.interpret.backend:config;

import forge.vm.wasm.interpret.allocator;
import forge.vm.wasm.interpret.debug_info;
import forge.vm.wasm.interpret.exceptions;
import forge.vm.wasm.interpret.host_function;
import forge.vm.wasm.interpret.options;
import forge.vm.wasm.interpret.types;
import forge.vm.wasm.interpret.watchdog;

namespace forge::vm::wasm::interpret {

// create constexpr flags for whether the backend should obey alignment hints
#ifdef FORGE_VM_WASM_INTERPRET_ALIGN_MEMORY_OPS
inline constexpr bool should_align_memory_ops = true;
#else
inline constexpr bool should_align_memory_ops = false;
#endif

inline constexpr bool use_softfloat = true;

#ifdef FORGE_VM_WASM_INTERPRET_FULL_DEBUG
inline constexpr bool wasm_debug = true;
#else
inline constexpr bool wasm_debug = false;
#endif

#ifdef __x86_64__
inline constexpr bool wasm_amd64 = true;
#else
inline constexpr bool wasm_amd64 = false;
#endif

} // namespace forge::vm::wasm::interpret
