module;

export module forge.vm.wasm.interpret.backend:sections;

import forge.vm.wasm.interpret.allocator;
import forge.vm.wasm.interpret.debug_info;
import forge.vm.wasm.interpret.exceptions;
import forge.vm.wasm.interpret.host_function;
import forge.vm.wasm.interpret.options;
import forge.vm.wasm.interpret.types;
import forge.vm.wasm.interpret.watchdog;

namespace forge::vm::wasm::interpret {
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
} // namespace forge::vm::wasm::interpret
