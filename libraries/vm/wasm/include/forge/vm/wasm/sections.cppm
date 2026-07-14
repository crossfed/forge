module;

export module forge.vm.wasm.backend:sections;

import forge.vm.wasm.allocator;
import forge.vm.wasm.debug_info;
import forge.vm.wasm.exceptions;
import forge.vm.wasm.host_function;
import forge.vm.wasm.options;
import forge.vm.wasm.types;
import forge.vm.wasm.watchdog;

namespace forge::vm::wasm {
enum section_id {
   custom_section = 0,
   type_section = 1,
   import_section = 2,
   function_section = 3,
   table_section = 4,
   memory_section = 5,
   global_section = 6,
   export_section = 7,
   start_section = 8,
   element_section = 9,
   code_section = 10,
   data_section = 11,
   num_of_elems
};
} // namespace forge::vm::wasm
