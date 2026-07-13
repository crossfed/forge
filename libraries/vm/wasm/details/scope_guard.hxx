#pragma once

#include <utility>

FORGE_VM_WASM_EXPORT namespace forge::vm::wasm {

template <typename Function>
struct scope_guard {
   explicit scope_guard(Function&& function) : function(std::move(function)) {}
   ~scope_guard() { function(); }

   Function function;
};

} // namespace forge::vm::wasm
