module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

export module forge.vm.wasm.debug_info;

export namespace forge::vm::wasm {

struct null_debug_info {
   using builder = null_debug_info;
   void on_code_start(const void* compiled_base, const void* wasm_code_start) {}
   void on_function_start(const void* code_addr, const void* wasm_addr) {}
   void on_instr_start(const void* code_addr, const void* wasm_addr) {}
   void on_code_end(const void* code_addr, const void* wasm_addr) {}
   void set(const null_debug_info&) {}
   void relocate(const void*) {}
};

// Maps a contiguous region of code to offsets onto the code section of the original wasm.
class profile_instr_map {
   struct addr_entry {
      uint32_t offset;
      uint32_t wasm_addr;
   };

public:

   struct builder {
      void on_code_start(const void* compiled_base, const void* wasm_code_start);
      void on_function_start(const void* code_addr, const void* wasm_addr);
      void on_instr_start(const void* code_addr, const void* wasm_addr);
      void on_code_end(const void* code_addr, const void* wasm_addr);

      const void* code_base = nullptr;
      const void* wasm_base = nullptr;
      const void* code_end = nullptr;
      std::vector<addr_entry> data;
   };

   void set(builder&& value);

   profile_instr_map() = default;
   profile_instr_map(const profile_instr_map&) = delete;
   profile_instr_map& operator=(const profile_instr_map&) = delete;

   // Indicate that the executable code was moved/copied/mmapped/etc to another location
   void relocate(const void* new_base);

   // Cannot use most of the standard library as the STL is not async-signal-safe
   std::uint32_t translate(const void* pc) const;
private:
   const void* base_address = nullptr;
   std::size_t code_size = 0;

   addr_entry* offset_to_addr = nullptr;
   std::size_t offset_to_addr_len = 0;

   std::vector<addr_entry> data;
};

} // namespace forge::vm::wasm
