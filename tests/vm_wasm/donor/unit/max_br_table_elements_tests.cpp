#include "test_prelude.hpp"
import forge.vm.wasm.allocator;
import forge.vm.wasm.stack_elem;
import forge.vm.wasm.utils;
import forge.vm.wasm.backend;
#define FORGE_VM_WASM_TEST_USES_BACKEND
#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE max_br_table_elements_tests

using namespace forge::vm::wasm;

extern wasm_allocator wa;

namespace {

/*
 * (module
 *   (func (br_table 0 0 0 (i32.const 0))
 * )
 */
std::vector<uint8_t> two_elements_wasm = {
   0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60,
   0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0x0a, 0x0b, 0x01, 0x09, 0x00, 0x41,
   0x00, 0x0e, 0x02, 0x00, 0x00, 0x00, 0x0b
};

struct empty_options {};
struct static_options_1 {
   static constexpr std::uint32_t max_br_table_elements = 1;
};
struct static_options_2 {
   static constexpr std::uint32_t max_br_table_elements = 2;
};
struct dynamic_options {
   std::uint32_t max_br_table_elements;
};

}

BACKEND_TEST_CASE("Test max_br_table_elements default", "[max_br_table_elements_test]") {
   using backend_t = backend<std::nullptr_t, TestType>;
   backend_t backend(two_elements_wasm, &wa);
}

BACKEND_TEST_CASE("Test max_br_table_elements unlimited", "[max_br_table_elements_test]") {
   using backend_t = backend<std::nullptr_t, TestType, empty_options>;
   backend_t backend(two_elements_wasm, &wa);
}

BACKEND_TEST_CASE("Test max_br_table_elements static fail", "[max_br_table_elements_test]") {
   using backend_t = backend<std::nullptr_t, TestType, static_options_1>;
   BOOST_CHECK_THROW(backend_t(two_elements_wasm, &wa), exceptions::parse);
}

BACKEND_TEST_CASE("Test max_br_table_elements static pass", "[max_br_table_elements_test]") {
   using backend_t = backend<std::nullptr_t, TestType, static_options_2>;
   backend_t backend(two_elements_wasm, &wa);
}

BACKEND_TEST_CASE("Test max_br_table_elements dynamic fail", "[max_br_table_elements_test]") {
   using backend_t = backend<std::nullptr_t, TestType, dynamic_options>;
   BOOST_CHECK_THROW(backend_t(two_elements_wasm, nullptr, dynamic_options{1}), exceptions::parse);
}

BACKEND_TEST_CASE("Test max_br_table_elements dynamic pass", "[max_br_table_elements_test]") {
   using backend_t = backend<std::nullptr_t, TestType, dynamic_options>;
   backend_t backend(two_elements_wasm, nullptr, dynamic_options{2});
}
