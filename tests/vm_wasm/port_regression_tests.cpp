#include "test_prelude.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

import forge.vm.wasm.allocator;
import forge.vm.wasm.backend;
import forge.vm.wasm.debug_info;
import forge.vm.wasm.types;
import forge.vm.wasm.vector;
import forge.vm.wasm.wasm_stack;

#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE port_regression_tests

namespace wasm = forge::vm::wasm;

static_assert(!std::is_copy_constructible_v<wasm::wasm_allocator>);
static_assert(!std::is_copy_assignable_v<wasm::wasm_allocator>);
static_assert(!std::is_move_constructible_v<wasm::wasm_allocator>);
static_assert(!std::is_move_assignable_v<wasm::wasm_allocator>);
static_assert(!std::is_copy_constructible_v<wasm::growable_allocator>);
static_assert(!std::is_copy_assignable_v<wasm::growable_allocator>);
static_assert(!std::is_move_constructible_v<wasm::growable_allocator>);
static_assert(!std::is_move_assignable_v<wasm::growable_allocator>);

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

struct zero_call_depth_options {
   std::uint32_t max_call_depth = 0;
};

struct eight_stack_bytes_options {
   static constexpr std::uint32_t max_func_local_bytes = 8;
   static constexpr auto max_func_local_bytes_flags = wasm::max_func_local_bytes_flags_t::stack;
};

struct shared_limits_options {
   std::uint32_t max_call_depth = 17;
   std::uint32_t max_pages = 2;
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

template <typename Implementation, typename Exception> void check_zero_call_depth() {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,       // header
       0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,             // () -> i32
       0x03, 0x02, 0x01, 0x00,                               // one function
       0x07, 0x07, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x00, // export run
       0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x2a, 0x0b        // return 42
   };
   using runtime = wasm::backend<std::nullptr_t, Implementation, zero_call_depth_options>;
   auto instance = runtime{code, static_cast<wasm::wasm_allocator*>(nullptr), zero_call_depth_options{}};

   BOOST_CHECK_THROW(instance.call_with_return("env", "run"), Exception);
}

template <typename Implementation> void check_numeric_function_index() {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,       // header
       0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,             // () -> i32
       0x03, 0x02, 0x01, 0x00,                               // one function
       0x07, 0x07, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x00, // export run
       0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x2a, 0x0b        // return 42
   };
   using runtime = wasm::backend<std::nullptr_t, Implementation>;
   auto instance = runtime{code, static_cast<wasm::wasm_allocator*>(nullptr)};

   BOOST_TEST(instance.call(static_cast<std::nullptr_t*>(nullptr), 0U));
   BOOST_CHECK_THROW(instance.call(static_cast<std::nullptr_t*>(nullptr), 1U), wasm::exceptions::interpreter);
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

template <typename Implementation> void check_start_without_memory() {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,                         // header
       0x01, 0x08, 0x02, 0x60, 0x00, 0x00, 0x60, 0x00, 0x01, 0x7f,             // function types
       0x03, 0x03, 0x02, 0x00, 0x01,                                           // start and getter functions
       0x06, 0x06, 0x01, 0x7f, 0x01, 0x41, 0x00, 0x0b,                         // mutable i32 global
       0x07, 0x07, 0x01, 0x03, 0x67, 0x65, 0x74, 0x00, 0x01,                   // export get
       0x08, 0x01, 0x00,                                                       // start function 0
       0x0a, 0x0d, 0x02, 0x06, 0x00, 0x41, 0x07, 0x24, 0x00, 0x0b, 0x04, 0x00, // set global to 7
       0x23, 0x00, 0x0b                                                        // return global
   };
   using runtime = wasm::backend<std::nullptr_t, Implementation>;
   auto instance = runtime{code, static_cast<wasm::wasm_allocator*>(nullptr)};

   const auto result = instance.call_with_return("env", "get");
   BOOST_REQUIRE(result.has_value());
   BOOST_TEST(result->to_ui32() == 7U);
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

TEST_CASE("pointer validation rejects absent linear memory", "[execution_interface]") {
   const auto interface = wasm::execution_interface{nullptr, nullptr};

   BOOST_CHECK_THROW(interface.validate_pointer<char>(wasm::wasm_ptr_t{0}, wasm::wasm_size_t{1}),
                     wasm::exceptions::memory);
   BOOST_CHECK_THROW(interface.validate_pointer<char>(wasm::wasm_ptr_t{0}, wasm::wasm_size_t{0}),
                     wasm::exceptions::memory);
   BOOST_CHECK_THROW(interface.validate_null_terminated_pointer(wasm::wasm_ptr_t{0}), wasm::exceptions::memory);
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

TEST_CASE("profile maps reject program counters outside their code range", "[debug_info]") {
   auto code = std::array<char, 8>{};
   auto wasm_code = std::array<char, 8>{};
   auto builder = wasm::profile_instr_map::builder{};
   builder.on_code_start(code.data(), wasm_code.data());
   builder.on_function_start(code.data() + 2, wasm_code.data() + 5);
   builder.on_code_end(code.data() + 6, wasm_code.data() + 6);

   auto map = wasm::profile_instr_map{};
   map.set(std::move(builder));

   const auto before = reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(code.data()) - 1);
   BOOST_TEST(map.translate(before) == std::numeric_limits<std::uint32_t>::max());
   BOOST_TEST(map.translate(code.data() + 1) == std::numeric_limits<std::uint32_t>::max());
   BOOST_TEST(map.translate(code.data() + 2) == 5U);
   BOOST_TEST(map.translate(code.data() + 6) == std::numeric_limits<std::uint32_t>::max());
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

TEST_CASE("managed vectors preserve values across growth", "[managed_vector]") {
   auto allocator = wasm::growable_allocator{64};
   auto values = wasm::managed_vector<std::uint32_t, wasm::growable_allocator>{allocator};

   values.push_back(7U);
   values.emplace_back(9U);
   values.push_back(11U);

   BOOST_TEST(values.size() == 3U);
   BOOST_TEST(values[0] == 7U);
   BOOST_TEST(values[1] == 9U);
   BOOST_TEST(values[2] == 11U);

   values.emplace_back(13U);

   BOOST_TEST(values.size() == 4U);
   BOOST_TEST(values[0] == 7U);
   BOOST_TEST(values[1] == 9U);
   BOOST_TEST(values[2] == 11U);
   BOOST_TEST(values[3] == 13U);
}

TEST_CASE("managed vector back returns the last appended element", "[managed_vector]") {
   auto allocator = wasm::growable_allocator{64};
   auto values = wasm::managed_vector<std::uint32_t, wasm::growable_allocator>{allocator};

   static_assert(std::is_same_v<decltype(values.back()), std::uint32_t&>);
   BOOST_CHECK_THROW(values.back(), wasm::exceptions::vector_out_of_bounds);

   values.push_back(7U);
   BOOST_TEST(values.back() == 7U);

   values.emplace_back(11U);
   BOOST_TEST(values.back() == 11U);

   values.back() = 13U;
   BOOST_TEST(values[1] == 13U);

   values.pop_back();
   BOOST_TEST(values.back() == 7U);
}

TEST_CASE("managed vector rejects empty pops without changing its index", "[managed_vector]") {
   auto allocator = wasm::growable_allocator{64};
   auto values = wasm::managed_vector<std::uint32_t, wasm::growable_allocator>{allocator};

   BOOST_CHECK_THROW(values.pop_back(), wasm::exceptions::vector_out_of_bounds);

   values.push_back(7U);
   values.pop_back();
   BOOST_CHECK_THROW(values.pop_back(), wasm::exceptions::vector_out_of_bounds);

   values.push_back(9U);
   BOOST_TEST(values[0] == 9U);
}

TEST_CASE("managed vector copy preserves the next insertion position", "[managed_vector]") {
   auto allocator = wasm::growable_allocator{64};
   auto values = wasm::managed_vector<std::uint32_t, wasm::growable_allocator>{allocator};
   auto source = std::array<std::uint32_t, 2>{7U, 9U};

   values.copy(source.data(), 0);
   BOOST_REQUIRE_THROW(values.pop_back(), wasm::exceptions::vector_out_of_bounds);
   values.push_back(5U);
   BOOST_TEST(values.size() == 1U);
   BOOST_TEST(values.back() == 5U);

   values.copy(source.data(), source.size());
   BOOST_TEST(values.back() == 9U);
   values.push_back(11U);
   BOOST_TEST(values.size() == 3U);
   BOOST_TEST(values[0] == 7U);
   BOOST_TEST(values[1] == 9U);
   BOOST_TEST(values[2] == 11U);
}

TEST_CASE("code protection failures surface as allocation errors", "[allocator]") {
   const auto page_size = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
   auto allocator = wasm::growable_allocator{};
   allocator.use_fixed_memory(page_size);
   auto* code = allocator.start_code();
   static_cast<void>(allocator.alloc<std::byte>(1));
   allocator.end_code<false>(code);
   const auto span = allocator.get_code_span();

   BOOST_REQUIRE(!span.empty());
   BOOST_REQUIRE(::munmap(span.data(), span.size()) == 0);
   BOOST_CHECK_THROW(allocator.disable_code(), wasm::exceptions::allocation);
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

TEST_CASE("null backend rejects deferred instantiation errors", "[null_backend]") {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,       // header
       0x01, 0x04, 0x01, 0x60, 0x00, 0x00,                   // one function type
       0x03, 0x02, 0x01, 0x00,                               // one function
       0x04, 0x04, 0x01, 0x70, 0x00, 0x01,                   // table with one element
       0x09, 0x07, 0x01, 0x00, 0x41, 0x01, 0x0b, 0x01, 0x00, // segment starts past the table
       0x0a, 0x04, 0x01, 0x02, 0x00, 0x0b                    // empty function body
   };
   using validator = wasm::backend<std::nullptr_t, wasm::null_backend>;

   const auto validate = [&] {
      auto instance = validator{code, static_cast<wasm::wasm_allocator*>(nullptr)};
      static_cast<void>(instance);
   };

   BOOST_CHECK_THROW(validate(), wasm::exceptions::interpreter);
}

TEST_CASE("unreachable operands do not consume the live stack byte limit", "[max_func_local_bytes]") {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // header
       0x01, 0x04, 0x01, 0x60, 0x00, 0x00,             // one function type
       0x03, 0x02, 0x01, 0x00,                         // one function
       0x0a, 0x0e, 0x01, 0x0c, 0x00,                   // code section and empty locals
       0x02, 0x40,                                     // block with no result
       0x42, 0x00,                                     // i64.const 0
       0x0c, 0x00,                                     // br 0 discards the i64
       0x0b,                                           // end block
       0x42, 0x00, 0x1a,                               // i64.const 0; drop
       0x0b                                            // end function
   };

   auto instance = wasm::backend<std::nullptr_t, wasm::interpreter, eight_stack_bytes_options>{code, nullptr};
}

template <typename Impl> void verify_public_call_indirect() {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,       // header
       0x01, 0x04, 0x01, 0x60, 0x00, 0x00,                   // one function type
       0x03, 0x02, 0x01, 0x00,                               // one function
       0x04, 0x04, 0x01, 0x70, 0x00, 0x01,                   // table with one element
       0x09, 0x07, 0x01, 0x00, 0x41, 0x00, 0x0b, 0x01, 0x00, // function zero at table index zero
       0x0a, 0x04, 0x01, 0x02, 0x00, 0x0b                    // empty function body
   };
   auto allocator = wasm::wasm_allocator{};
   {
      auto instance = wasm::backend<std::nullptr_t, Impl>{code, &allocator};

      BOOST_TEST(instance.call_indirect(nullptr, 0));
      BOOST_CHECK_THROW(instance.call_indirect(nullptr, 1), wasm::exceptions::interpreter);
   }
   allocator.free();
}

template <typename Impl> void verify_public_call_indirect_rejects_empty_slot() {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // header
       0x04, 0x04, 0x01, 0x70, 0x00, 0x01              // table with one empty element
   };
   auto allocator = wasm::wasm_allocator{};
   {
      auto instance = wasm::backend<std::nullptr_t, Impl>{code, &allocator};

      BOOST_CHECK_THROW(instance.call_indirect(nullptr, 0), wasm::exceptions::interpreter);
   }
   allocator.free();
}

TEST_CASE("interpreter exposes public call indirect", "[call_indirect]") {
   verify_public_call_indirect<wasm::interpreter>();
   verify_public_call_indirect_rejects_empty_slot<wasm::interpreter>();
}

#if FORGE_VM_WASM_HAS_JIT
TEST_CASE("jit exposes public call indirect", "[call_indirect]") {
   verify_public_call_indirect<wasm::jit>();
   verify_public_call_indirect_rejects_empty_slot<wasm::jit>();
}
#endif

TEST_CASE("data segments must fit declared linear memory", "[null_backend]") {
   auto invalid_code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,      // header
       0x05, 0x03, 0x01, 0x00, 0x00,                        // zero-page linear memory
       0x0b, 0x07, 0x01, 0x00, 0x41, 0x00, 0x0b, 0x01, 0x2a // one byte at offset zero
   };
   using validator = wasm::backend<std::nullptr_t, wasm::null_backend>;
   using interpreter = wasm::backend<std::nullptr_t, wasm::interpreter>;

   const auto validate = [&] {
      auto code = invalid_code;
      auto instance = validator{code, static_cast<wasm::wasm_allocator*>(nullptr)};
      static_cast<void>(instance);
   };
   BOOST_CHECK_THROW(validate(), wasm::exceptions::memory);

   auto memory = wasm::wasm_allocator{};
   const auto instantiate = [&] {
      auto code = invalid_code;
      auto instance = interpreter{code, &memory};
      static_cast<void>(instance);
   };
   BOOST_CHECK_THROW(instantiate(), wasm::exceptions::memory);
   memory.free();

   auto valid_code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,      // header
       0x05, 0x03, 0x01, 0x00, 0x01,                        // one-page linear memory
       0x0b, 0x07, 0x01, 0x00, 0x41, 0x00, 0x0b, 0x01, 0x2a // one byte at offset zero
   };
   auto valid = validator{valid_code, static_cast<wasm::wasm_allocator*>(nullptr)};
   static_cast<void>(valid);
}

TEST_CASE("stack rejects indexes outside its live range", "[stack]") {
   auto values = wasm::stack<std::uint32_t, 4>{};

   BOOST_CHECK_THROW(values.get(0), wasm::exceptions::interpreter);
   BOOST_CHECK_THROW(values.set(0, 7), wasm::exceptions::interpreter);

   values.push(11);
   BOOST_TEST(values.get(0) == 11U);
   BOOST_CHECK_THROW(values.get(1), wasm::exceptions::interpreter);
   BOOST_CHECK_THROW(values.set(1, 13), wasm::exceptions::interpreter);
   BOOST_TEST(values.size() == 1U);

   values.set(0, 17);
   BOOST_TEST(values.get(0) == 17U);
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

TEST_CASE("interpreter executes start functions without linear memory", "[backend]") {
   check_start_without_memory<wasm::interpreter>();
}

TEST_CASE("interpreter rejects linear memory without an allocator", "[backend]") {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,       // header
       0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,             // () -> i32
       0x03, 0x02, 0x01, 0x00,                               // one function
       0x05, 0x03, 0x01, 0x00, 0x01,                         // one-page linear memory
       0x07, 0x07, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x00, // export run
       0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x00, 0x0b        // return 0
   };
   using runtime = wasm::backend<std::nullptr_t, wasm::interpreter>;
   auto instance = runtime{code, static_cast<wasm::wasm_allocator*>(nullptr)};

   BOOST_CHECK_THROW(instance.initialize(), wasm::exceptions::allocation);
   BOOST_CHECK_THROW(instance.call_with_return("env", "run"), wasm::exceptions::allocation);

   auto memory = wasm::wasm_allocator{};
   instance.set_wasm_allocator(&memory);
   BOOST_CHECK_THROW(instance.call_with_return("env", "run"), wasm::exceptions::interpreter);
   instance.initialize();
   const auto result = instance.call_with_return("env", "run");
   BOOST_REQUIRE(result.has_value());
   BOOST_TEST(result->to_ui32() == 0u);
   memory.free();
}

TEST_CASE("shared modules accept non-owning execution contexts", "[backend]") {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,       // header
       0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,             // () -> i32
       0x03, 0x02, 0x01, 0x00,                               // one function
       0x07, 0x07, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x00, // export run
       0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x2a, 0x0b        // return 42
   };
   using runtime = wasm::backend<std::nullptr_t, wasm::interpreter, shared_limits_options>;
   auto source = runtime{code, static_cast<wasm::wasm_allocator*>(nullptr), shared_limits_options{}};
   auto shared = runtime{};
   shared.share(source);

   using context = std::remove_cvref_t<decltype(source.get_context())>;
   auto execution = context{shared.get_module(), 1024};
   auto memory = wasm::wasm_allocator{};
   shared.set_context(&execution);
   shared.set_wasm_allocator(&memory);
   shared.reset_max_call_depth();
   shared.initialize();

   const auto result = shared.call_with_return("env", "run");
   BOOST_REQUIRE(result.has_value());
   BOOST_TEST(result->to_ui32() == 42u);
   memory.free();
}

TEST_CASE("shared modules preserve their initial page limit", "[backend]") {
   auto code = wasm::wasm_code{
       0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // header
       0x05, 0x03, 0x01, 0x00, 0x01                    // one-page linear memory
   };
   using runtime = wasm::backend<std::nullptr_t, wasm::interpreter, shared_limits_options>;
   auto source = runtime{code, static_cast<wasm::wasm_allocator*>(nullptr), shared_limits_options{}};
   auto shared = runtime{};
   shared.share(source);

   using context = std::remove_cvref_t<decltype(source.get_context())>;
   auto execution = context{shared.get_module(), 1024};
   auto memory = wasm::wasm_allocator{};
   shared.set_context(&execution);
   shared.set_wasm_allocator(&memory);
   shared.reset_max_pages();
   BOOST_CHECK_NO_THROW(shared.initialize());
   memory.free();
}

TEST_CASE("interpreter rejects zero call depth", "[execution_context]") {
   check_zero_call_depth<wasm::interpreter, wasm::exceptions::vector_out_of_bounds>();
}

TEST_CASE("interpreter rejects out-of-range numeric function indexes", "[execution_context]") {
   check_numeric_function_index<wasm::interpreter>();
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

TEST_CASE("jit executes start functions without linear memory", "[backend]") {
   check_start_without_memory<wasm::jit>();
}

TEST_CASE("jit rejects zero call depth", "[execution_context]") {
   check_zero_call_depth<wasm::jit, wasm::exceptions::interpreter>();
}

TEST_CASE("jit rejects out-of-range numeric function indexes", "[execution_context]") {
   check_numeric_function_index<wasm::jit>();
}
#endif
