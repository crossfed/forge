module;

#include <cstdint>

module forge.vm.wasm.interpret.types;

namespace forge::vm::wasm::interpret {

void module::normalize_types() {
   type_aliases.resize(types.size());
   for (std::uint32_t i = 0; i < types.size(); ++i) {
      std::uint32_t canonical = 0;
      for (; canonical < i; ++canonical) {
         if (types[canonical] == types[i]) {
            break;
         }
      }
      type_aliases[i] = canonical;
   }

   const auto imported_functions_size = get_imported_functions_size();
   fast_functions.resize(functions.size() + imported_functions_size);
   for (std::uint32_t i = 0; i < imported_functions_size; ++i) {
      fast_functions[i] = type_aliases[imports[i].type.func_t];
   }
   for (std::uint32_t i = 0; i < functions.size(); ++i) {
      fast_functions[i + imported_functions_size] = type_aliases[functions[i]];
   }
}

} // namespace forge::vm::wasm::interpret
