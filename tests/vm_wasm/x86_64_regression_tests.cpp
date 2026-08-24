module;

#include "test_prelude.hpp"

module forge.vm.wasm.interpret.backend;

import :execution_context;
import :parser;
import :x86_64;
import forge.vm.wasm.interpret.utils;

#include "test_support.hpp"

#define FORGE_VM_WASM_INTERPRET_TEST_FILE x86_64_regression_tests

namespace wasm = forge::vm::wasm::interpret;

namespace forge::vm::wasm::interpret {

struct no_popcnt_cpu_features {
   static bool has_tzcnt() {
      return false;
   }

   static bool has_popcnt() {
      return false;
   }
};

struct portable_popcnt_jit {
   template <typename Host> using context = jit_execution_context<Host>;
   template <typename Host, typename Options, typename DebugInfo>
   using parser =
       binary_parser<machine_code_writer<jit_execution_context<Host>, no_popcnt_cpu_features>, Options, DebugInfo>;
   static constexpr bool is_jit = true;
};

} // namespace forge::vm::wasm::interpret

TEST_CASE("jit executes integer popcount without the popcnt CPU feature", "[x86_64]") {
   using backend = wasm::backend<wasm::standalone_function_t, wasm::portable_popcnt_jit>;

   auto i32_allocator = wasm::wasm_allocator{};
   {
      auto i32_code = wasm::read_wasm(std::string(wasm::wasm_directory) + "i32.0.wasm");
      auto i32_backend = backend{i32_code, &i32_allocator};
      BOOST_TEST(i32_backend.call_with_return("env", "popcnt", UINT32_C(3735928559))->to_ui32() == UINT32_C(24));
   }
   i32_allocator.free();

   auto i64_allocator = wasm::wasm_allocator{};
   {
      auto i64_code = wasm::read_wasm(std::string(wasm::wasm_directory) + "i64.0.wasm");
      auto i64_backend = backend{i64_code, &i64_allocator};
      BOOST_TEST(i64_backend.call_with_return("env", "popcnt", UINT64_C(16045690984833335023))->to_ui64() ==
                 UINT64_C(48));
   }
   i64_allocator.free();
}
