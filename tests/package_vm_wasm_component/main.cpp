#include <concepts>
#include <cstddef>

import forge.vm.wasm.backend;

int main() {
   using engine = forge::vm::wasm::backend<std::nullptr_t, forge::vm::wasm::interpreter>;
   static_assert(std::default_initializable<engine>);
   static_assert(!forge::vm::wasm::interpreter::is_jit);
   return 0;
}
