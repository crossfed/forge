#pragma once

namespace forge::vm::wasm {

// create constexpr flags for whether the backend should obey alignment hints
#ifdef FORGE_VM_WASM_ALIGN_MEMORY_OPS
   inline constexpr bool should_align_memory_ops = true;
#else
   inline constexpr bool should_align_memory_ops = false;
#endif


   inline constexpr bool use_softfloat = true;

#ifdef FORGE_VM_WASM_FULL_DEBUG
   inline constexpr bool wasm_debug = true;
#else
   inline constexpr bool wasm_debug = false;
#endif

#ifdef __x86_64__
   inline constexpr bool wasm_amd64 = true;
#else
   inline constexpr bool wasm_amd64 = false;
#endif

} // namespace forge::vm::wasm
