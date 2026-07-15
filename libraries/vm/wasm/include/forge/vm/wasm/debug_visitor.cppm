module;

#include <forge/vm/wasm/opcode_macros.hpp>
#include <iostream>

export module forge.vm.wasm.backend:debug_visitor;

import forge.vm.wasm.allocator;
import forge.vm.wasm.debug_info;
import forge.vm.wasm.exceptions;
import forge.vm.wasm.host_function;
import forge.vm.wasm.options;
import forge.vm.wasm.types;
import forge.vm.wasm.watchdog;
import :interpret_visitor;

#define DBG_VISIT(name, code)                                                                                          \
   void operator()(const FORGE_VM_WASM_OPCODE_T(name) & op) {                                                          \
      std::cout << "Found " << #name << " at " << get_context().get_pc() << "\n";                                      \
      interpret_visitor<ExecutionCTX>::operator()(op);                                                                 \
      get_context().print_stack();                                                                                     \
   }

#define DBG2_VISIT(name, code)                                                                                         \
   void operator()(const FORGE_VM_WASM_OPCODE_T(name) & op) {                                                          \
      std::cout << "Found " << #name << "\n";                                                                          \
   }

namespace forge::vm::wasm {

template <typename ExecutionCTX> struct debug_visitor : public interpret_visitor<ExecutionCTX> {
   using interpret_visitor<ExecutionCTX>::operator();
   debug_visitor(ExecutionCTX& ctx) : interpret_visitor<ExecutionCTX>(ctx) {}
   ExecutionCTX& get_context() {
      return interpret_visitor<ExecutionCTX>::get_context();
   }
   FORGE_VM_WASM_CONTROL_FLOW_OPS(DBG_VISIT)
   FORGE_VM_WASM_BR_TABLE_OP(DBG_VISIT)
   FORGE_VM_WASM_RETURN_OP(DBG_VISIT)
   FORGE_VM_WASM_CALL_OPS(DBG_VISIT)
   FORGE_VM_WASM_CALL_IMM_OPS(DBG_VISIT)
   FORGE_VM_WASM_PARAMETRIC_OPS(DBG_VISIT)
   FORGE_VM_WASM_VARIABLE_ACCESS_OPS(DBG_VISIT)
   FORGE_VM_WASM_MEMORY_OPS(DBG_VISIT)
   FORGE_VM_WASM_I32_CONSTANT_OPS(DBG_VISIT)
   FORGE_VM_WASM_I64_CONSTANT_OPS(DBG_VISIT)
   FORGE_VM_WASM_F32_CONSTANT_OPS(DBG_VISIT)
   FORGE_VM_WASM_F64_CONSTANT_OPS(DBG_VISIT)
   FORGE_VM_WASM_COMPARISON_OPS(DBG_VISIT)
   FORGE_VM_WASM_NUMERIC_OPS(DBG_VISIT)
   FORGE_VM_WASM_CONVERSION_OPS(DBG_VISIT)
   FORGE_VM_WASM_EXIT_OP(DBG_VISIT)
   FORGE_VM_WASM_ERROR_OPS(DBG_VISIT)
};

struct debug_visitor2 {
   FORGE_VM_WASM_CONTROL_FLOW_OPS(DBG2_VISIT)
   FORGE_VM_WASM_BR_TABLE_OP(DBG2_VISIT)
   FORGE_VM_WASM_RETURN_OP(DBG2_VISIT)
   FORGE_VM_WASM_CALL_OPS(DBG2_VISIT)
   FORGE_VM_WASM_CALL_IMM_OPS(DBG2_VISIT)
   FORGE_VM_WASM_PARAMETRIC_OPS(DBG2_VISIT)
   FORGE_VM_WASM_VARIABLE_ACCESS_OPS(DBG2_VISIT)
   FORGE_VM_WASM_MEMORY_OPS(DBG2_VISIT)
   FORGE_VM_WASM_I32_CONSTANT_OPS(DBG2_VISIT)
   FORGE_VM_WASM_I64_CONSTANT_OPS(DBG2_VISIT)
   FORGE_VM_WASM_F32_CONSTANT_OPS(DBG2_VISIT)
   FORGE_VM_WASM_F64_CONSTANT_OPS(DBG2_VISIT)
   FORGE_VM_WASM_COMPARISON_OPS(DBG2_VISIT)
   FORGE_VM_WASM_NUMERIC_OPS(DBG2_VISIT)
   FORGE_VM_WASM_CONVERSION_OPS(DBG2_VISIT)
   FORGE_VM_WASM_EXIT_OP(DBG2_VISIT)
   FORGE_VM_WASM_ERROR_OPS(DBG2_VISIT)
};
#undef DBG_VISIT
#undef DBG2_VISIT

#undef DBG_VISIT
#undef DBG2_VISIT

} // namespace forge::vm::wasm
