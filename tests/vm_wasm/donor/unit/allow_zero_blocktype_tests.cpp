#include "test_prelude.hpp"
import forge.vm.wasm.interpret.allocator;
import forge.vm.wasm.interpret.stack_elem;
import forge.vm.wasm.interpret.utils;
import forge.vm.wasm.interpret.backend;
#define FORGE_VM_WASM_INTERPRET_TEST_USES_BACKEND
#include "test_support.hpp"

#define FORGE_VM_WASM_INTERPRET_TEST_FILE allow_zero_blocktype_tests

using namespace forge::vm::wasm::interpret;

extern wasm_allocator wa;

namespace {

/*
 * (module
 *   (func (block))
 * )
 */
std::vector<uint8_t> block_wasm = {
   0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60,
   0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0x0a, 0x07, 0x01, 0x05, 0x00, 0x02,
   0x00 /* was 0x40 */, 0x0b, 0x0b
};

/*
 * (module
 *   (func (loop))
 * )
 */
std::vector<uint8_t> loop_wasm = {
   0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60,
   0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0x0a, 0x07, 0x01, 0x05, 0x00, 0x03,
   0x00 /* was 0x40 */, 0x0b, 0x0b
};

/*
 * (module
 *   (func (if (i32.const 0) (then)))
 * )
 */
std::vector<uint8_t> if_wasm = {
   0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x60,
   0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00, 0x41,
   0x00, 0x04, 0x00 /* was 0x40 */, 0x0b, 0x0b
};

struct empty_options {};
struct static_options_false {
   static constexpr bool allow_zero_blocktype = false;
};
struct static_options_true {
   static constexpr std::uint32_t allow_zero_blocktype = true;
};
struct dynamic_options {
   bool allow_zero_blocktype;
};

}

BACKEND_TEST_CASE("Test allow_zero_blocktype default", "[allow_zero_blocktype_test]") {
   using backend_t = backend<std::nullptr_t, TestType>;
   BOOST_CHECK_THROW(backend_t(block_wasm, &wa), exceptions::parse);
   BOOST_CHECK_THROW(backend_t(loop_wasm, &wa), exceptions::parse);
   BOOST_CHECK_THROW(backend_t(if_wasm, &wa), exceptions::parse);
}

BACKEND_TEST_CASE("Test allow_zero_blocktype empty", "[allow_zero_blocktype_test]") {
   using backend_t = backend<std::nullptr_t, TestType, empty_options>;
   BOOST_CHECK_THROW(backend_t(block_wasm, &wa), exceptions::parse);
   BOOST_CHECK_THROW(backend_t(loop_wasm, &wa), exceptions::parse);
   BOOST_CHECK_THROW(backend_t(if_wasm, &wa), exceptions::parse);
}

BACKEND_TEST_CASE("Test allow_zero_blocktype static fail", "[allow_zero_blocktype_test]") {
   using backend_t = backend<std::nullptr_t, TestType, static_options_false>;
   BOOST_CHECK_THROW(backend_t(block_wasm, &wa), exceptions::parse);
   BOOST_CHECK_THROW(backend_t(loop_wasm, &wa), exceptions::parse);
   BOOST_CHECK_THROW(backend_t(if_wasm, &wa), exceptions::parse);
}

BACKEND_TEST_CASE("Test allow_zero_blocktype static pass", "[allow_zero_blocktype_test]") {
   using backend_t = backend<std::nullptr_t, TestType, static_options_true>;
   backend_t backend_block(block_wasm, &wa);
   backend_t backend_loop(loop_wasm, &wa);
   backend_t backend_if(if_wasm, &wa);
}

BACKEND_TEST_CASE("Test allow_zero_blocktype dynamic fail", "[allow_zero_blocktype_test]") {
   using backend_t = backend<std::nullptr_t, TestType, dynamic_options>;
   BOOST_CHECK_THROW(backend_t(block_wasm, nullptr, dynamic_options{false}), exceptions::parse);
   BOOST_CHECK_THROW(backend_t(loop_wasm, nullptr, dynamic_options{false}), exceptions::parse);
   BOOST_CHECK_THROW(backend_t(if_wasm, nullptr, dynamic_options{false}), exceptions::parse);
}

BACKEND_TEST_CASE("Test allow_zero_blocktype dynamic pass", "[allow_zero_blocktype_test]") {
   using backend_t = backend<std::nullptr_t, TestType, dynamic_options>;
   backend_t backend_block(block_wasm, nullptr, dynamic_options{true});
   backend_t backend_loop(loop_wasm, nullptr, dynamic_options{true});
   backend_t backend_if(if_wasm, nullptr, dynamic_options{true});
}
