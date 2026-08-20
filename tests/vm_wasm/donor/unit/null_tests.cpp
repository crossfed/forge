#include "test_prelude.hpp"
import forge.vm.wasm.interpret.allocator;
import forge.vm.wasm.interpret.stack_elem;
import forge.vm.wasm.interpret.utils;
import forge.vm.wasm.interpret.backend;
#include "test_support.hpp"

#define FORGE_VM_WASM_INTERPRET_TEST_FILE null_tests

using namespace forge::vm::wasm::interpret;

extern wasm_allocator wa;

TEST_CASE("Tests a null backend", "[null_backend]") {
   /*
    * (module)
    */
   std::vector<uint8_t> code = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00 };

   using backend_t = backend<std::nullptr_t, null_backend>;
   backend_t bkend(code, nullptr);
}
