#pragma once

#define FORGE_VM_WASM_FROM_WASM_ADD_TAG(...) (__VA_ARGS__, ::forge::vm::wasm::tag<T> = {})

#define FORGE_VM_WASM_FROM_WASM(TYPE, PARAMS)                                                                          \
   template <typename T>                                                                                               \
   auto from_wasm FORGE_VM_WASM_FROM_WASM_ADD_TAG PARAMS const->std::enable_if_t<std::is_same_v<T, TYPE>, TYPE>

#define FORGE_VM_WASM_INVOKE_ON(TYPE, CONDITION) ::forge::vm::wasm::invoke_on<false, TYPE>(CONDITION, args...)

#define FORGE_VM_WASM_INVOKE_ON_ALL(CONDITION)                                                                         \
   ::forge::vm::wasm::invoke_on<false, ::forge::vm::wasm::invoke_on_all_t>(CONDITION, args...)

#define FORGE_VM_WASM_INVOKE_ONCE(CONDITION)                                                                           \
   ::forge::vm::wasm::invoke_on<true, ::forge::vm::wasm::invoke_on_all_t>(CONDITION, args...)

#define FORGE_VM_WASM_PRECONDITION(NAME, ...)                                                                          \
   struct NAME {                                                                                                       \
      template <typename TypeConverter, typename... Args>                                                              \
      static decltype(auto) condition(TypeConverter& context, const Args&... args) {                                   \
         __VA_ARGS__;                                                                                                  \
      }                                                                                                                \
   }
