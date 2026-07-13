#include "test_prelude.hpp"
import forge.vm.wasm.backend;
#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE eosio_max_nested_structures_tests

using namespace forge::vm::wasm;

extern wasm_allocator wa;

namespace {

/*
 * (module
 *   (func)
 *   (func
 *     (if (i32.const 0) (then) (else (drop (i32.const 0))))
 *     (block (block))
 *   )
 * )
 */
std::vector<uint8_t> nested_4_wasm = {
   0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60,
   0x00, 0x00, 0x03, 0x03, 0x02, 0x00, 0x00, 0x0a, 0x16, 0x02, 0x02, 0x00,
   0x0b, 0x11, 0x00, 0x41, 0x00, 0x04, 0x40, 0x05, 0x41, 0x00, 0x1a, 0x0b,
   0x02, 0x40, 0x02, 0x40, 0x0b, 0x0b, 0x0b
};

/*
 * (module
 *   (func
 *     (if (i32.const 0) (then) (else (drop (i32.const 0))))
 *     (block (block (block)))
 *   )
 * )
 */
std::vector<uint8_t> nested_4_wasm_2 = {
  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60,
  0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0x0a, 0x16, 0x01, 0x14, 0x00, 0x41,
  0x00, 0x04, 0x40, 0x05, 0x41, 0x00, 0x1a, 0x0b, 0x02, 0x40, 0x02, 0x40,
  0x02, 0x40, 0x0b, 0x0b, 0x0b, 0x0b
};

struct empty_options {};
struct static_options_3 {
   static constexpr std::uint32_t max_control_depth = 3;
};
struct static_options_4 {
   static constexpr std::uint32_t max_control_depth = 4;
};
struct dynamic_options {
   std::uint32_t max_control_depth;
};

}

BACKEND_TEST_CASE("Test max_control_depth default", "[max_control_depth_test]") {
   using backend_t = backend<std::nullptr_t, TestType>;
   backend_t backend(nested_4_wasm, &wa);
   backend_t backend2(nested_4_wasm_2, &wa);
}

BACKEND_TEST_CASE("Test max_control_depth unlimited", "[max_control_depth_test]") {
   using backend_t = backend<std::nullptr_t, TestType, empty_options>;
   backend_t backend(nested_4_wasm, &wa);
   backend_t backend2(nested_4_wasm_2, &wa);
}

BACKEND_TEST_CASE("Test max_control_depth static fail", "[max_control_depth_test]") {
   using backend_t = backend<std::nullptr_t, TestType, static_options_3>;
   BOOST_CHECK_THROW(backend_t(nested_4_wasm, &wa), exceptions::parse);
   BOOST_CHECK_THROW(backend_t(nested_4_wasm_2, &wa), exceptions::parse);
}

BACKEND_TEST_CASE("Test max_control_depth static pass", "[max_control_depth_test]") {
   using backend_t = backend<std::nullptr_t, TestType, static_options_4>;
   backend_t backend(nested_4_wasm, &wa);
   backend_t backend2(nested_4_wasm_2, &wa);
}

BACKEND_TEST_CASE("Test max_control_depth dynamic fail", "[max_control_depth_test]") {
   using backend_t = backend<std::nullptr_t, TestType, dynamic_options>;
   BOOST_CHECK_THROW(backend_t(nested_4_wasm, nullptr, dynamic_options{3}), exceptions::parse);
   BOOST_CHECK_THROW(backend_t(nested_4_wasm_2, nullptr, dynamic_options{3}), exceptions::parse);
}

BACKEND_TEST_CASE("Test max_control_depth dynamic pass", "[max_control_depth_test]") {
   using backend_t = backend<std::nullptr_t, TestType, dynamic_options>;
   backend_t backend(nested_4_wasm, nullptr, dynamic_options{4});
   backend_t backend2(nested_4_wasm_2, nullptr, dynamic_options{4});
}
