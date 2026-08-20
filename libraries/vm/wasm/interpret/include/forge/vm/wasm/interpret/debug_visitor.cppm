module;

#include <forge/vm/wasm/interpret/opcode_macros.hpp>
#include <iostream>

export module forge.vm.wasm.interpret.backend:debug_visitor;

import forge.vm.wasm.interpret.allocator;
import forge.vm.wasm.interpret.debug_info;
import forge.vm.wasm.interpret.exceptions;
import forge.vm.wasm.interpret.host_function;
import forge.vm.wasm.interpret.options;
import forge.vm.wasm.interpret.types;
import forge.vm.wasm.interpret.watchdog;
import :interpret_visitor;

#define DBG_VISIT(name, code)                                                                                          \
   void operator()(const FORGE_VM_WASM_INTERPRET_OPCODE_T(name) & op) {                                                          \
      std::cout << "Found " << #name << " at " << get_context().get_pc() << "\n";                                      \
      interpret_visitor<ExecutionCTX>::operator()(op);                                                                 \
      get_context().print_stack();                                                                                     \
   }

#define DBG2_VISIT(name, code)                                                                                         \
   void operator()(const FORGE_VM_WASM_INTERPRET_OPCODE_T(name) & op) {                                                          \
      std::cout << "Found " << #name << "\n";                                                                          \
   }

namespace forge::vm::wasm::interpret {

template <typename ExecutionCTX> struct debug_visitor : public interpret_visitor<ExecutionCTX> {
   using interpret_visitor<ExecutionCTX>::operator();
   debug_visitor(ExecutionCTX& ctx) : interpret_visitor<ExecutionCTX>(ctx) {}
   ExecutionCTX& get_context() {
      return interpret_visitor<ExecutionCTX>::get_context();
   }
   FORGE_VM_WASM_INTERPRET_CONTROL_FLOW_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_BR_TABLE_OP(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_RETURN_OP(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_CALL_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_CALL_IMM_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_PARAMETRIC_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_VARIABLE_ACCESS_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_MEMORY_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_I32_CONSTANT_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_I64_CONSTANT_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_F32_CONSTANT_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_F64_CONSTANT_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_COMPARISON_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_NUMERIC_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_CONVERSION_OPS(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_EXIT_OP(DBG_VISIT)
   FORGE_VM_WASM_INTERPRET_ERROR_OPS(DBG_VISIT)
};

struct debug_visitor2 {
   FORGE_VM_WASM_INTERPRET_CONTROL_FLOW_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_BR_TABLE_OP(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_RETURN_OP(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_CALL_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_CALL_IMM_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_PARAMETRIC_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_VARIABLE_ACCESS_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_MEMORY_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_I32_CONSTANT_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_I64_CONSTANT_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_F32_CONSTANT_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_F64_CONSTANT_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_COMPARISON_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_NUMERIC_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_CONVERSION_OPS(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_EXIT_OP(DBG2_VISIT)
   FORGE_VM_WASM_INTERPRET_ERROR_OPS(DBG2_VISIT)
};
#undef DBG_VISIT
#undef DBG2_VISIT

#undef DBG_VISIT
#undef DBG2_VISIT

} // namespace forge::vm::wasm::interpret
