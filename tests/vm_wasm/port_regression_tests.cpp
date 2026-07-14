#include "test_prelude.hpp"

#include <limits>
#include <string>
#include <utility>
#include <vector>

import forge.vm.wasm.allocator;
import forge.vm.wasm.types;
import forge.vm.wasm.vector;

#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE port_regression_tests

namespace wasm = forge::vm::wasm;

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
