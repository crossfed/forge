module;

#include "test_prelude.hpp"

module forge.vm.wasm.backend;

import :signals;
import forge.vm.wasm.allocator;

#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE signals_regression_tests

namespace wasm = forge::vm::wasm;

TEST_CASE("nested signal scopes restore the outer memory range", "[signals]") {
   auto outer_memory = wasm::wasm_allocator{};
   auto inner_memory = wasm::wasm_allocator{};

   wasm::invoke_with_signal_handler(
       [&] {
          const auto expected = wasm::memory_range;
          wasm::invoke_with_signal_handler([] {}, [](int) {}, nullptr, &inner_memory);

          BOOST_TEST(wasm::memory_range.data() == expected.data());
          BOOST_TEST(wasm::memory_range.size() == expected.size());
       },
       [](int) {}, nullptr, &outer_memory);

   outer_memory.free();
   inner_memory.free();
}
