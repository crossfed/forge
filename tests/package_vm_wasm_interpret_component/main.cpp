#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

import forge.vm.wasm.interpret.backend;

namespace {
struct host {
   std::int32_t ping(std::int32_t value) {
      return value;
   }

   void write_log(forge::vm::wasm::interpret::span<const char> text) {
      static_cast<void>(std::string_view{text.data(), text.size()});
   }
};

struct linear_memory {
   linear_memory(const linear_memory&) = delete;
   linear_memory& operator=(const linear_memory&) = delete;

   linear_memory() = default;

   forge::vm::wasm::interpret::wasm_allocator value;
};
} // namespace

int main() {
   using host_functions = forge::vm::wasm::interpret::registered_host_functions<host>;
   using engine = forge::vm::wasm::interpret::backend<host_functions, forge::vm::wasm::interpret::interpreter,
                                                      forge::vm::wasm::interpret::compatibility_options>;
   using validator = forge::vm::wasm::interpret::backend<std::nullptr_t, forge::vm::wasm::interpret::null_backend,
                                                         forge::vm::wasm::interpret::compatibility_options>;

   host_functions::add<&host::ping>("env", "ping");
   host_functions::add<&host::write_log>("env", "write_log");

   forge::vm::wasm::interpret::wasm_code code{0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
   auto parsed = validator{code, static_cast<forge::vm::wasm::interpret::wasm_allocator*>(nullptr)};

   host instance;
   linear_memory memory;
   engine backend{code, instance, &memory.value};
   auto deadline = forge::vm::wasm::interpret::watchdog{std::chrono::milliseconds{10}};

   backend.timed_run(deadline, [] {});

   static_assert(!forge::vm::wasm::interpret::interpreter::is_jit);
   (void)parsed.get_module();
   (void)backend.get_module();
   return 0;
}
