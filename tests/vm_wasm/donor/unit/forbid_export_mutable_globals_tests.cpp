#include "test_prelude.hpp"
import forge.vm.wasm.interpret.allocator;
import forge.vm.wasm.interpret.stack_elem;
import forge.vm.wasm.interpret.utils;
import forge.vm.wasm.interpret.backend;
#define FORGE_VM_WASM_INTERPRET_TEST_USES_BACKEND
#include "test_support.hpp"

#define FORGE_VM_WASM_INTERPRET_TEST_FILE forbid_export_mutable_globals_tests

using namespace forge::vm::wasm::interpret;

extern wasm_allocator wa;

namespace {

/*
 * (module
 *  (global (export "i") (mut i32) (i32.const 0))
 * )
 */
std::vector<uint8_t> mut_global_wasm = {
   0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x06, 0x06, 0x01, 0x7f,
   0x01, 0x41, 0x00, 0x0b, 0x07, 0x05, 0x01, 0x01, 0x69, 0x03, 0x00
};

struct empty_options {};
struct static_options_false {
   static constexpr bool forbid_export_mutable_globals = false;
};
struct static_options_true {
   static constexpr bool forbid_export_mutable_globals = true;
};
struct dynamic_options {
   bool forbid_export_mutable_globals;
};

}

BACKEND_TEST_CASE("Test forbid_export_mutable_globals default", "[forbid_export_mutable_globals_test]") {
   using backend_t = backend<std::nullptr_t, TestType>;
   backend_t backend(mut_global_wasm, &wa);
}

BACKEND_TEST_CASE("Test forbid_export_mutable_globals empty", "[forbid_export_mutable_globals_test]") {
   using backend_t = backend<std::nullptr_t, TestType, empty_options>;
   backend_t backend(mut_global_wasm, &wa);
}

BACKEND_TEST_CASE("Test forbid_export_mutable_globals static pass", "[forbid_export_mutable_globals_test]") {
   using backend_t = backend<std::nullptr_t, TestType, static_options_false>;
   backend_t backend(mut_global_wasm, &wa);
}

BACKEND_TEST_CASE("Test forbid_export_mutable_globals static fail", "[forbid_export_mutable_globals_test]") {
   using backend_t = backend<std::nullptr_t, TestType, static_options_true>;
   BOOST_CHECK_THROW(backend_t(mut_global_wasm, &wa), exceptions::parse);
}

BACKEND_TEST_CASE("Test forbid_export_mutable_globals dynamic pass", "[forbid_export_mutable_globals_test]") {
   using backend_t = backend<std::nullptr_t, TestType, dynamic_options>;
   backend_t backend(mut_global_wasm, nullptr, dynamic_options{false});
}

BACKEND_TEST_CASE("Test forbid_export_mutable_globals dynamic fail", "[forbid_export_mutable_globals_test]") {
   using backend_t = backend<std::nullptr_t, TestType, dynamic_options>;
   BOOST_CHECK_THROW(backend_t(mut_global_wasm, nullptr, dynamic_options{true}), exceptions::parse);
}
