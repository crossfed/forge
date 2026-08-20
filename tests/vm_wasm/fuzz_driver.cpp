#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <new>

import forge.vm.wasm.interpret.backend;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
   using namespace forge::vm::wasm::interpret;

   auto allocator = wasm_allocator{};
   auto code = wasm_code{};
   code.resize(size);
   std::memcpy(code.data(), data, size);

   try {
      auto vm = backend<std::nullptr_t, interpreter>{code, &allocator};
      (void)vm;
   } catch (const forge::exceptions::base&) {
      // Invalid bytecode is an expected fuzz input, not a harness failure.
   }
   return 0;
}
