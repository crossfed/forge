#include "test_prelude.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

import forge.vm.wasm.allocator;
import forge.vm.wasm.backend;
import forge.vm.wasm.debug_info;
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

struct conversion_host {
   std::uint32_t offset = 0;
};

struct converted_argument {
   std::uint32_t value = 0;
};

struct active_host_converter : wasm::type_converter<conversion_host> {
   using type_converter::to_wasm;
   using type_converter::type_converter;

   std::uint32_t to_wasm(converted_argument value) {
      return this->host ? value.value + this->get_host().offset : 0;
   }
};

using conversion_functions =
    wasm::registered_host_functions<conversion_host, wasm::execution_interface, active_host_converter>;

template <typename Implementation> void check_active_host_conversion() {
   auto code = wasm::wasm_code{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,             // header
                               0x01, 0x06, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7f,             // function type
                               0x03, 0x02, 0x01, 0x00,                                     // function
                               0x07, 0x08, 0x01, 0x04, 0x65, 0x63, 0x68, 0x6f, 0x00, 0x00, // export echo
                               0x0a, 0x06, 0x01, 0x04, 0x00, 0x20, 0x00, 0x0b};            // return argument
   using runtime = wasm::backend<conversion_functions, Implementation>;
   auto host = conversion_host{7};
   auto memory = wasm::wasm_allocator{};
   {
      auto instance = runtime{code, host, &memory};
      const auto result = instance.call_with_return(host, "env", "echo", converted_argument{5});

      BOOST_TEST(result->to_ui32() == 12U);
   }
   memory.free();
}

template <typename Implementation> void check_execute_all_with_host() {
   auto code = wasm::wasm_code{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // header
                               0x01, 0x04, 0x01, 0x60, 0x00, 0x00,             // function type
                               0x03, 0x02, 0x01, 0x00,                         // function
                               0x07, 0x07, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, // export run
                               0x00, 0x0a, 0x04, 0x01, 0x02, 0x00, 0x0b};      // empty body
   using runtime = wasm::backend<conversion_functions, Implementation>;
   auto host = conversion_host{};
   auto memory = wasm::wasm_allocator{};
   {
      auto instance = runtime{code, host, &memory};
      instance.execute_all(wasm::null_watchdog{}, host);
   }
   memory.free();
}

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

TEST_CASE("empty profile maps translate to the unknown address", "[debug_info]") {
   auto code = std::array<char, 1>{};
   auto builder = wasm::profile_instr_map::builder{};
   builder.on_code_start(code.data(), code.data());
   builder.on_code_end(code.data() + code.size(), code.data());

   auto map = wasm::profile_instr_map{};
   map.set(std::move(builder));

   BOOST_TEST(map.translate(code.data()) == std::numeric_limits<std::uint32_t>::max());
}

TEST_CASE("default span proxies copy and write back without an alignment divisor", "[argument_proxy]") {
   auto values = std::array<std::uint32_t, 2>{1, 2};
   {
      auto proxy = wasm::argument_proxy<wasm::span<std::uint32_t>>{values.data(), values.size()};
      BOOST_TEST(proxy.data() != values.data());
      proxy[0] = 9;
   }

   BOOST_TEST(values[0] == 9U);
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

TEST_CASE("managed vectors grow from zero capacity", "[managed_vector]") {
   auto allocator = wasm::growable_allocator{64};
   auto values = wasm::managed_vector<std::uint32_t, wasm::growable_allocator>{allocator};

   values.push_back(7U);

   BOOST_TEST(values.size() == 1U);
   BOOST_TEST(values[0] == 7U);
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

TEST_CASE("interpreter argument conversion receives the active host", "[execution_context]") {
   check_active_host_conversion<wasm::interpreter>();
}

TEST_CASE("interpreter execute all supplies the active host", "[backend]") {
   check_execute_all_with_host<wasm::interpreter>();
}

#if FORGE_VM_WASM_HAS_JIT && !defined(FORGE_VM_WASM_TEST_INTERPRETER_ONLY)
TEST_CASE("jit reports a missing export before function type lookup", "[execution_context]") {
   auto code = wasm::wasm_code{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
   using runtime = wasm::backend<std::nullptr_t, wasm::jit>;
   auto instance = runtime{code, static_cast<wasm::wasm_allocator*>(nullptr)};

   BOOST_CHECK_THROW(instance("env", "missing"), wasm::exceptions::interpreter);
}

TEST_CASE("jit memory grow treats its operand as an unsigned page count", "[execution_context]") {
   check_oversized_memory_grow<wasm::jit>();
}

TEST_CASE("jit argument conversion receives the active host", "[execution_context]") {
   check_active_host_conversion<wasm::jit>();
}

TEST_CASE("jit execute all supplies the active host", "[backend]") {
   check_execute_all_with_host<wasm::jit>();
}
#endif
