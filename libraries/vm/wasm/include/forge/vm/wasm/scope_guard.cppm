module;

#include <utility>

export module forge.vm.wasm.scope_guard;

export namespace forge::vm::wasm {

template <typename Function> struct scope_guard {
   explicit scope_guard(Function&& function) : function(std::move(function)) {}
   ~scope_guard() {
      function();
   }

   Function function;
};

} // namespace forge::vm::wasm
