#include <cstddef>
#include <cstdint>

import forge.vm.wasm.backend;

namespace {
struct host {
   std::int32_t ping(std::int32_t value) {
      return value;
   }
};
} // namespace

int main() {
   using host_functions = forge::vm::wasm::registered_host_functions<host>;
   using engine = forge::vm::wasm::backend<host_functions, forge::vm::wasm::interpreter>;

   host_functions::add<&host::ping>("env", "ping");

   forge::vm::wasm::wasm_allocator allocator;
   forge::vm::wasm::wasm_code code{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
   host instance;
   engine backend{code, instance, &allocator};

   static_assert(!forge::vm::wasm::interpreter::is_jit);
   (void)backend.get_module();
   return 0;
}
