#include "test_prelude.hpp"
import forge.vm.wasm.backend;
#include "test_support.hpp"

#define FORGE_VM_WASM_TEST_FILE null_tests

using namespace forge::vm::wasm;

extern wasm_allocator wa;

TEST_CASE("Tests a null backend", "[null_backend]") {
   /*
    * (module)
    */
   std::vector<uint8_t> code = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00 };

   using backend_t = backend<std::nullptr_t, null_backend>;
   backend_t bkend(code, nullptr);
}
