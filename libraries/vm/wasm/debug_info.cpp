module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

module forge.vm.wasm.debug_info;

namespace forge::vm::wasm {

void profile_instr_map::builder::on_code_start(const void* compiled_base, const void* wasm_code_start) {
   code_base = compiled_base;
   wasm_base = wasm_code_start;
}

void profile_instr_map::builder::on_function_start(const void* code_addr, const void* wasm_addr) {
   data.push_back({
       static_cast<std::uint32_t>(reinterpret_cast<const char*>(code_addr) - reinterpret_cast<const char*>(code_base)),
       static_cast<std::uint32_t>(reinterpret_cast<const char*>(wasm_addr) - reinterpret_cast<const char*>(wasm_base)),
   });
}

void profile_instr_map::builder::on_instr_start(const void* code_addr, const void* wasm_addr) {
   data.push_back({
       static_cast<std::uint32_t>(reinterpret_cast<const char*>(code_addr) - reinterpret_cast<const char*>(code_base)),
       static_cast<std::uint32_t>(reinterpret_cast<const char*>(wasm_addr) - reinterpret_cast<const char*>(wasm_base)),
   });
}

void profile_instr_map::builder::on_code_end(const void* code_addr, const void*) {
   code_end = code_addr;
}

void profile_instr_map::set(builder&& value) {
   data = std::move(value.data);
   std::sort(data.begin(), data.end(),
             [](const addr_entry& lhs, const addr_entry& rhs) { return lhs.offset < rhs.offset; });
   base_address = value.code_base;
   code_size = reinterpret_cast<const char*>(value.code_end) - reinterpret_cast<const char*>(base_address);
   offset_to_addr = data.data();
   offset_to_addr_len = data.size();
}

void profile_instr_map::relocate(const void* new_base) {
   base_address = new_base;
}

std::uint32_t profile_instr_map::translate(const void* pc) const {
   const auto diff =
       static_cast<std::size_t>(reinterpret_cast<const char*>(pc) - reinterpret_cast<const char*>(base_address));
   if (diff >= code_size || diff < offset_to_addr[0].offset) {
      return 0xFFFFFFFFu;
   }

   const auto offset = static_cast<std::uint32_t>(diff);
   std::size_t lower = 0;
   std::size_t upper = offset_to_addr_len;
   while (upper - lower > 1) {
      const auto middle = lower + (upper - lower) / 2;
      if (offset_to_addr[middle].offset <= offset) {
         lower = middle;
      } else {
         upper = middle;
      }
   }
   return offset_to_addr[lower].wasm_addr;
}

} // namespace forge::vm::wasm
