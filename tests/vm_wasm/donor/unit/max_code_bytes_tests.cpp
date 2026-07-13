#include "test_prelude.hpp"
import forge.vm.wasm.backend;
#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE max_code_bytes_tests

using namespace forge::vm::wasm;

extern wasm_allocator wa;

namespace {

/*
 * (module
 *   (func (local i32))
 * )
 */
std::vector<uint8_t> func_wasm = {
   0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60,
   0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0x0a, 0x06, 0x01, 0x04, 0x01, 0x01,
   0x7f, 0x0b
};

struct empty_options {};
struct static_options_3 {
   static constexpr std::uint32_t max_code_bytes = 3;
};
struct static_options_4 {
   static constexpr std::uint32_t max_code_bytes = 4;
};
struct dynamic_options {
   std::uint32_t max_code_bytes;
};

}

BACKEND_TEST_CASE("Test max_code_bytes default", "[max_code_bytes_test]") {
   using backend_t = backend<std::nullptr_t, TestType>;
   backend_t backend(func_wasm, &wa);
}

BACKEND_TEST_CASE("Test max_code_bytes unlimited", "[max_code_bytes_test]") {
   using backend_t = backend<std::nullptr_t, TestType, empty_options>;
   backend_t backend(func_wasm, &wa);
}

BACKEND_TEST_CASE("Test max_code_bytes static fail", "[max_code_bytes_test]") {
   using backend_t = backend<std::nullptr_t, TestType, static_options_3>;
   BOOST_CHECK_THROW(backend_t(func_wasm, &wa), exceptions::parse);
}

BACKEND_TEST_CASE("Test max_code_bytes static pass", "[max_code_bytes_test]") {
   using backend_t = backend<std::nullptr_t, TestType, static_options_4>;
   backend_t backend(func_wasm, &wa);
}

BACKEND_TEST_CASE("Test max_code_bytes dynamic fail", "[max_code_bytes_test]") {
   using backend_t = backend<std::nullptr_t, TestType, dynamic_options>;
   BOOST_CHECK_THROW(backend_t(func_wasm, nullptr, dynamic_options{3}), exceptions::parse);
}

BACKEND_TEST_CASE("Test max_code_bytes dynamic pass", "[max_code_bytes_test]") {
   using backend_t = backend<std::nullptr_t, TestType, dynamic_options>;
   backend_t backend(func_wasm, nullptr, dynamic_options{4});
}
