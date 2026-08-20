module;

#include <utility>

export module forge.vm.wasm.interpret.scope_guard;

export namespace forge::vm::wasm::interpret {

template <typename Function> struct scope_guard {
   explicit scope_guard(Function&& function) : function(std::move(function)) {}
   ~scope_guard() {
      function();
   }

   Function function;
};

} // namespace forge::vm::wasm::interpret
