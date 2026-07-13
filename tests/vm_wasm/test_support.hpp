#pragma once

#define FORGE_VM_WASM_JOIN_IMPL(lhs, rhs) lhs##rhs
#define FORGE_VM_WASM_JOIN(lhs, rhs) FORGE_VM_WASM_JOIN_IMPL(lhs, rhs)
#define FORGE_VM_WASM_TEST_NAME(line)                                                                                   \
   FORGE_VM_WASM_JOIN(FORGE_VM_WASM_JOIN(FORGE_VM_WASM_TEST_FILE, _line_), line)

#define TEST_CASE(name, tags) BOOST_AUTO_TEST_CASE(FORGE_VM_WASM_TEST_NAME(__LINE__))

#if !defined(FORGE_VM_WASM_INTERNAL_TESTS) && FORGE_VM_WASM_HAS_JIT &&                                      \
   !defined(FORGE_VM_WASM_TEST_INTERPRETER_ONLY)
using forge_vm_wasm_backend_types = boost::mpl::list<forge::vm::wasm::interpreter, forge::vm::wasm::jit>;
#elif !defined(FORGE_VM_WASM_INTERNAL_TESTS)
using forge_vm_wasm_backend_types = boost::mpl::list<forge::vm::wasm::interpreter>;
#endif

#define BACKEND_TEST_CASE(name, tags)                                                                                   \
   BOOST_AUTO_TEST_CASE_TEMPLATE(FORGE_VM_WASM_TEST_NAME(__LINE__), TestType, forge_vm_wasm_backend_types)

struct type_converter32 {
   union {
      std::uint32_t ui;
      float f;
   } data;

   explicit type_converter32(std::uint32_t value) { data.ui = value; }
   std::uint32_t to_ui() const { return data.ui; }
   float to_f() const { return data.f; }
};

struct type_converter64 {
   union {
      std::uint64_t ui;
      double f;
   } data;

   explicit type_converter64(std::uint64_t value) { data.ui = value; }
   std::uint64_t to_ui() const { return data.ui; }
   double to_f() const { return data.f; }
};

template <typename T, typename U>
T bit_cast(const U& value) {
   static_assert(sizeof(T) == sizeof(U), "bitcast requires identical sizes");
   auto result = T{};
   std::memcpy(&result, &value, sizeof(T));
   return result;
}

inline bool check_nan(const std::optional<forge::vm::wasm::operand_stack_elem>& value) {
   return visit(
      forge::vm::wasm::overloaded{
         [](forge::vm::wasm::i32_const_t) { return false; },
         [](forge::vm::wasm::i64_const_t) { return false; },
         [](forge::vm::wasm::f32_const_t item) { return std::isnan(item.data.f); },
         [](forge::vm::wasm::f64_const_t item) { return std::isnan(item.data.f); },
      },
      *value
   );
}

inline forge::vm::wasm::wasm_allocator* get_wasm_allocator() {
   static auto allocator = forge::vm::wasm::wasm_allocator{};
   return &allocator;
}

namespace forge::vm::wasm {
inline constexpr auto host_wasm = FORGE_VM_WASM_HOST_WASM;
inline constexpr auto wasm_directory = FORGE_VM_WASM_FIXTURE_DIRECTORY;
} // namespace forge::vm::wasm
