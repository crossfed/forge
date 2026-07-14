#include "test_prelude.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

import forge.vm.wasm.allocator;
import forge.vm.wasm.backend;
import forge.vm.wasm.types;
import forge.vm.wasm.vector;

#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE port_regression_tests

namespace wasm = forge::vm::wasm;

namespace {
struct empty_span_host {
   void accept(wasm::span<const char> value) {
      called = value.empty();
   }

   bool called = false;
};

template <typename Implementation> void check_oversized_memory_grow() {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,                                     // header
       0x01, 0x0a, 0x02, 0x60, 0x01, 0x7f, 0x01, 0x7f, 0x60, 0x00, 0x01, 0x7f,             // function types
       0x03, 0x03, 0x02, 0x00, 0x01,                                                       // functions
       0x05, 0x03, 0x01, 0x00, 0x01,                                                       // one memory page
       0x07, 0x0f, 0x02, 0x04, 0x67, 0x72, 0x6f, 0x77, 0x00, 0x00, 0x04, 0x73, 0x69, 0x7a, // exports
       0x65, 0x00, 0x01, 0x0a, 0x0d, 0x02, 0x06, 0x00, 0x20, 0x00, 0x40, 0x00, 0x0b, 0x04, 0x00, 0x3f, 0x00, // code
       0x0b};
   using runtime = wasm::backend<std::nullptr_t, Implementation>;
   auto memory = wasm::wasm_allocator{};
   {
      auto instance = runtime{code, &memory};
      const auto failed = instance.call_with_return("env", "grow", std::numeric_limits<std::uint32_t>::max());
      const auto size = instance.call_with_return("env", "size");

      BOOST_TEST(failed->to_ui32() == std::numeric_limits<std::uint32_t>::max());
      BOOST_TEST(size->to_ui32() == 1U);
   }
   memory.free();
}
} // namespace

TEST_CASE("function type equality includes non-void result types", "[func_type]") {
   auto lhs_allocator = wasm::growable_allocator{64};
   auto rhs_allocator = wasm::growable_allocator{64};
   auto lhs_parameters = wasm::guarded_vector<wasm::value_type>{lhs_allocator, 1};
   auto rhs_parameters = wasm::guarded_vector<wasm::value_type>{rhs_allocator, 1};
   lhs_parameters[0] = wasm::i32;
   rhs_parameters[0] = wasm::i32;

   const auto lhs = wasm::func_type{wasm::func, std::move(lhs_parameters), 1, wasm::i32};
   const auto rhs = wasm::func_type{wasm::func, std::move(rhs_parameters), 1, wasm::i64};

   BOOST_TEST(static_cast<bool>(lhs != rhs));
}

TEST_CASE("function type equality ignores absent result types", "[func_type]") {
   auto lhs_allocator = wasm::growable_allocator{64};
   auto rhs_allocator = wasm::growable_allocator{64};
   auto lhs_parameters = wasm::guarded_vector<wasm::value_type>{lhs_allocator, 1};
   auto rhs_parameters = wasm::guarded_vector<wasm::value_type>{rhs_allocator, 1};
   lhs_parameters[0] = wasm::i32;
   rhs_parameters[0] = wasm::i32;

   const auto lhs = wasm::func_type{wasm::func, std::move(lhs_parameters), 0, wasm::i32};
   const auto rhs = wasm::func_type{wasm::func, std::move(rhs_parameters), 0, wasm::i64};

   BOOST_TEST(static_cast<bool>(lhs == rhs));
}

TEST_CASE("vector to string sizes its destination", "[vector_to_string]") {
   const auto input = std::vector<char>{'w', 'a', 's', 'm'};

   BOOST_TEST(wasm::vector_to_string(input) == "wasm");
}

TEST_CASE("alternate stack allocation reports mapping failure", "[stack_allocator]") {
   constexpr auto impossible_size = std::numeric_limits<std::size_t>::max() / 2;

   BOOST_CHECK_THROW(wasm::stack_allocator{impossible_size}, wasm::exceptions::allocation);
}

TEST_CASE("data segments reject unsupported memory indexes", "[parser]") {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // header
       0x05, 0x03, 0x01, 0x00, 0x01,                   // one memory
       0x0b, 0x06, 0x01, 0x01, 0x41, 0x00, 0x0b, 0x00  // data segment for memory index 1
   };
   using validator = wasm::backend<std::nullptr_t, wasm::null_backend>;

   const auto parse = [&] {
      auto instance = validator{code, static_cast<wasm::wasm_allocator*>(nullptr)};
      static_cast<void>(instance);
   };

   BOOST_CHECK_THROW(parse(), wasm::exceptions::parse);
}

TEST_CASE("zero length host spans do not probe guest memory", "[execution_interface]") {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,                         // header
       0x01, 0x09, 0x02, 0x60, 0x02, 0x7f, 0x7f, 0x00, 0x60, 0x00, 0x00,       // function types
       0x02, 0x0e, 0x01, 0x03, 0x65, 0x6e, 0x76, 0x06, 0x61, 0x63, 0x63, 0x65, // import env.accept
       0x70, 0x74, 0x00, 0x00, 0x03, 0x02, 0x01, 0x01,                         // imported/local functions
       0x05, 0x03, 0x01, 0x00, 0x01,                                           // one memory
       0x07, 0x07, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x01,                   // export run
       0x0a, 0x0a, 0x01, 0x08, 0x00, 0x41, 0x00, 0x41, 0x00, 0x10, 0x00, 0x0b  // accept(0, 0)
   };
   using host_functions = wasm::registered_host_functions<empty_span_host>;
   using interpreter = wasm::backend<host_functions, wasm::interpreter>;
   host_functions::add<&empty_span_host::accept>("env", "accept");

   auto host = empty_span_host{};
   auto memory = wasm::wasm_allocator{};
   {
      auto instance = interpreter{code, host, &memory};
      instance(host, "env", "run");
   }
   memory.free();

   BOOST_TEST(host.called);
}

TEST_CASE("memory grow treats its operand as an unsigned page count", "[execution_context]") {
   check_oversized_memory_grow<wasm::interpreter>();
}

#if defined(__x86_64__)
TEST_CASE("jit reports a missing export before function type lookup", "[execution_context]") {
   auto code = wasm::wasm_code{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
   using runtime = wasm::backend<std::nullptr_t, wasm::jit>;
   auto instance = runtime{code, static_cast<wasm::wasm_allocator*>(nullptr)};

   BOOST_CHECK_THROW(instance("env", "missing"), wasm::exceptions::interpreter);
}

TEST_CASE("jit memory grow treats its operand as an unsigned page count", "[execution_context]") {
   check_oversized_memory_grow<wasm::jit>();
}
#endif
