#pragma once

#define FORGE_VM_WASM_INTERPRET_HAS_MEMBER(ARG, NAME)                                                                            \
   forge::vm::wasm::interpret::detail::overloaded{                                                                                \
       [](auto&& f,                                                                                                    \
          std::enable_if_t<std::is_class_v<std::decay_t<decltype(f)>>&&                                                \
                               forge::vm::wasm::interpret::detail::pass_type<decltype(&std::decay_t<decltype(f)>::type::NAME)>(), \
                           int> = 0) constexpr { return true; },                                                       \
       [](...) constexpr { return false; }}(forge::vm::wasm::interpret::detail::wrapper_t<decltype(ARG)>{})

#define FORGE_VM_WASM_INTERPRET_HAS_MEMBER_TY(TY, NAME)                                                                          \
   forge::vm::wasm::interpret::detail::overloaded{                                                                                \
       [](auto&& f,                                                                                                    \
          std::enable_if_t<std::is_class_v<TY>&&                                                                       \
                               forge::vm::wasm::interpret::detail::pass_type<decltype(&std::decay_t<decltype(f)>::type::NAME)>(), \
                           int> = 0) constexpr { return true; },                                                       \
       [](...) constexpr { return false; }}(forge::vm::wasm::interpret::detail::wrapper_t<TY>{})

#define FORGE_VM_WASM_INTERPRET_HAS_TEMPLATE_MEMBER(ARG, NAME)                                                                   \
   forge::vm::wasm::interpret::detail::overloaded{                                                                                \
       [&](auto&& f, std::enable_if_t<std::is_class_v<std::decay_t<decltype(f)>>&& forge::vm::wasm::interpret::detail::pass_type< \
                                          decltype(&std::decay_t<decltype(f)>::type::template NAME)>(),                \
                                      int> = 0) constexpr { return true; },                                            \
       [](...) constexpr { return false; }}(forge::vm::wasm::interpret::detail::wrapper_t<decltype(ARG)>{})

#define FORGE_VM_WASM_INTERPRET_HAS_TEMPLATE_MEMBER_TY(TY, NAME)                                                                 \
   forge::vm::wasm::interpret::detail::overloaded{                                                                                \
       [](auto&& f, std::enable_if_t<std::is_class_v<TY>&& forge::vm::wasm::interpret::detail::pass_type<                         \
                                         decltype(&std::decay_t<decltype(f)>::type::template NAME)>(),                 \
                                     int> = 0) constexpr { return true; },                                             \
       [](...) constexpr { return false; }}(forge::vm::wasm::interpret::detail::wrapper_t<TY>{})

// Work around old Clang handling of C++17 auto template parameters.
#define FORGE_VM_WASM_INTERPRET_AUTO_PARAM(X) forge::vm::wasm::interpret::detail::make_dependent<decltype(X)>(X)

#define FORGE_VM_WASM_INTERPRET_FROM_WASM_ADD_TAG(...) (__VA_ARGS__, ::forge::vm::wasm::interpret::tag<T> = {})

#define FORGE_VM_WASM_INTERPRET_FROM_WASM(TYPE, PARAMS)                                                                          \
   template <typename T>                                                                                               \
   auto from_wasm FORGE_VM_WASM_INTERPRET_FROM_WASM_ADD_TAG PARAMS const->std::enable_if_t<std::is_same_v<T, TYPE>, TYPE>

#define FORGE_VM_WASM_INTERPRET_INVOKE_ON(TYPE, CONDITION) ::forge::vm::wasm::interpret::invoke_on<false, TYPE>(CONDITION, args...)

#define FORGE_VM_WASM_INTERPRET_INVOKE_ON_ALL(CONDITION)                                                                         \
   ::forge::vm::wasm::interpret::invoke_on<false, ::forge::vm::wasm::interpret::invoke_on_all_t>(CONDITION, args...)

#define FORGE_VM_WASM_INTERPRET_INVOKE_ONCE(CONDITION)                                                                           \
   ::forge::vm::wasm::interpret::invoke_on<true, ::forge::vm::wasm::interpret::invoke_on_all_t>(CONDITION, args...)

#define FORGE_VM_WASM_INTERPRET_PRECONDITION(NAME, ...)                                                                          \
   struct NAME {                                                                                                       \
      template <typename TypeConverter, typename... Args>                                                              \
      static decltype(auto) condition(TypeConverter& context, const Args&... args) {                                   \
         __VA_ARGS__;                                                                                                  \
      }                                                                                                                \
   }
