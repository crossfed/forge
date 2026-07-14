module;

#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <forge/vm/wasm/opcode_macros.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <setjmp.h>
#include <signal.h>
#include <softfloat.hpp>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <ucontext.h>
#include <utility>
#include <variant>
#include <vector>

#if defined(__x86_64__)
#include <cpuid.h>
#endif

export module forge.vm.wasm.backend;

export import forge.vm.wasm.allocator;
export import forge.vm.wasm.debug_info;
export import forge.vm.wasm.exceptions;
export import forge.vm.wasm.host_function;
export import forge.vm.wasm.options;
export import forge.vm.wasm.types;
export import forge.vm.wasm.watchdog;

namespace forge::vm::wasm::detail {
template <typename Exception, typename Message>
[[noreturn]] inline void fail(Message&& message, std::source_location location = std::source_location::current()) {
   throw Exception{std::string{std::forward<Message>(message)}, {}, location};
}

template <typename Exception, typename Message>
inline void check(bool expression, Message&& message, std::source_location location = std::source_location::current()) {
   if (!expression) [[unlikely]] {
      fail<Exception>(std::forward<Message>(message), location);
   }
}
} // namespace forge::vm::wasm::detail

namespace forge::vm::wasm {

class bitcode_writer {

   template <class I> decltype(auto) append_instr(I&& instr) {
      return (fb[op_index++] = instr).template get<std::decay_t<I>>();
   }

 public:
   explicit bitcode_writer(growable_allocator& alloc, std::size_t source_bytes, module& mod)
       : _allocator(alloc), _code_segment_base(alloc.start_code()), fb(alloc, source_bytes), _mod(&mod) {}
   ~bitcode_writer() {
      _allocator.end_code<false>(_code_segment_base);
   }
   void emit_unreachable() {
      fb[op_index++] = unreachable_t{};
   };
   void emit_nop() {
      fb[op_index++] = nop_t{};
   }
   uint32_t emit_end() {
      return op_index;
   }
   uint32_t* emit_return(uint32_t depth_change) {
      return emit_br(depth_change);
   }
   void emit_block() {}
   uint32_t emit_loop() {
      return op_index;
   }
   uint32_t* emit_if() {
      if_t& instr = append_instr(if_t{});
      return &instr.pc;
   }
   uint32_t* emit_else(uint32_t* if_loc) {
      auto& else_ = append_instr(else_t{});
      *if_loc = _base_offset + op_index;
      return &else_.pc;
   }
   uint32_t* emit_br(uint32_t depth_change) {
      auto& instr = append_instr(br_t{});
      instr.data = depth_change;
      return &instr.pc;
   }
   uint32_t* emit_br_if(uint32_t depth_change) {
      auto& instr = append_instr(br_if_t{});
      instr.data = depth_change;
      return &instr.pc;
   }

   struct br_table_parser;
   friend struct br_table_parser;
   struct br_table_parser {
      br_table_parser(bitcode_writer& base, uint32_t table_size) : _this{&base}, _i{0} {
         br_table_t& bt = _this->append_instr(br_table_t{});
         bt.offset = static_cast<uint32_t>(((table_size * sizeof(br_table_t::elem_t)) / sizeof(opcode)) + 2);

         // point the branch table data to after the br_table instruction
         _br_tab = bt.table = reinterpret_cast<br_table_t::elem_t*>(&_this->fb[_this->op_index]);

         _this->op_index += bt.offset;

         // canary to throw if we have overbounded our allocated memory
         _this->fb[_this->op_index] = error_t{};
         bt.size = table_size;
      }
      uint32_t* emit_case(uint32_t depth_change) {
         auto& elem = _br_tab[_i++];
         elem.stack_pop = depth_change;
         return &elem.pc;
      }
      // Must be called after all cases
      uint32_t* emit_default(uint32_t depth_change) {
         auto result = emit_case(depth_change);
         detail::check<exceptions::parse>((_this->fb[_this->op_index].is_a<error_t>()), "overwrote br_table data");
         return result;
      }
      br_table_t::elem_t* _br_tab;
      bitcode_writer* _this;
      std::size_t _i;
      br_table_parser(const br_table_parser&) = delete;
      br_table_parser& operator=(const br_table_parser&) = delete;
   };
   auto emit_br_table(uint32_t table_size) {
      return br_table_parser{*this, table_size};
   }
   void emit_call(const func_type& ft, uint32_t funcnum) {
      fb[op_index++] = call_t{funcnum};
   }
   void emit_call_indirect(const func_type& ft, uint32_t functypeidx) {
      fb[op_index++] = call_indirect_t{functypeidx};
   }

   void emit_drop() {
      fb[op_index++] = drop_t{};
   }
   void emit_select() {
      fb[op_index++] = select_t{};
   }
   void emit_get_local(uint32_t localidx) {
      fb[op_index++] = get_local_t{localidx};
   }
   void emit_set_local(uint32_t localidx) {
      fb[op_index++] = set_local_t{localidx};
   }
   void emit_tee_local(uint32_t localidx) {
      fb[op_index++] = tee_local_t{localidx};
   }
   void emit_get_global(uint32_t localidx) {
      fb[op_index++] = get_global_t{localidx};
   }
   void emit_set_global(uint32_t localidx) {
      fb[op_index++] = set_global_t{localidx};
   }

#define MEM_OP(op_name)                                                                                                \
   void emit_##op_name(uint32_t offset, uint32_t alignment) {                                                          \
      fb[op_index++] = op_name##_t{offset, alignment};                                                                 \
   }
#define LOAD_OP MEM_OP
#define STORE_OP MEM_OP
   LOAD_OP(i32_load)
   LOAD_OP(i64_load)
   LOAD_OP(f32_load)
   LOAD_OP(f64_load)
   LOAD_OP(i32_load8_s)
   LOAD_OP(i32_load16_s)
   LOAD_OP(i32_load8_u)
   LOAD_OP(i32_load16_u)
   LOAD_OP(i64_load8_s)
   LOAD_OP(i64_load16_s)
   LOAD_OP(i64_load32_s)
   LOAD_OP(i64_load8_u)
   LOAD_OP(i64_load16_u)
   LOAD_OP(i64_load32_u)
   STORE_OP(i32_store)
   STORE_OP(i64_store)
   STORE_OP(f32_store)
   STORE_OP(f64_store)
   STORE_OP(i32_store8)
   STORE_OP(i32_store16)
   STORE_OP(i64_store8)
   STORE_OP(i64_store16)
   STORE_OP(i64_store32)
#undef LOAD_OP
#undef STORE_OP
#undef MEM_OP

   void emit_current_memory() {
      fb[op_index++] = current_memory_t{};
   }
   void emit_grow_memory() {
      fb[op_index++] = grow_memory_t{};
   }

   void emit_i32_const(uint32_t value) {
      fb[op_index++] = i32_const_t{value};
   }
   void emit_i64_const(uint64_t value) {
      fb[op_index++] = i64_const_t{value};
   }
   void emit_f32_const(float value) {
      fb[op_index++] = f32_const_t{value};
   }
   void emit_f64_const(double value) {
      fb[op_index++] = f64_const_t{value};
   }

#define OP(opname)                                                                                                     \
   void emit_##opname() {                                                                                              \
      fb[op_index++] = opname##_t{};                                                                                   \
   }
#define UNOP OP
#define BINOP OP

   UNOP(i32_eqz)
   BINOP(i32_eq)
   BINOP(i32_ne)
   BINOP(i32_lt_s)
   BINOP(i32_lt_u)
   BINOP(i32_gt_s)
   BINOP(i32_gt_u)
   BINOP(i32_le_s)
   BINOP(i32_le_u)
   BINOP(i32_ge_s)
   BINOP(i32_ge_u)
   UNOP(i64_eqz)
   BINOP(i64_eq)
   BINOP(i64_ne)
   BINOP(i64_lt_s)
   BINOP(i64_lt_u)
   BINOP(i64_gt_s)
   BINOP(i64_gt_u)
   BINOP(i64_le_s)
   BINOP(i64_le_u)
   BINOP(i64_ge_s)
   BINOP(i64_ge_u)
   BINOP(f32_eq)
   BINOP(f32_ne)
   BINOP(f32_lt)
   BINOP(f32_gt)
   BINOP(f32_le)
   BINOP(f32_ge)
   BINOP(f64_eq)
   BINOP(f64_ne)
   BINOP(f64_lt)
   BINOP(f64_gt)
   BINOP(f64_le)
   BINOP(f64_ge)

   UNOP(i32_clz)
   UNOP(i32_ctz)
   UNOP(i32_popcnt)
   BINOP(i32_add)
   BINOP(i32_sub)
   BINOP(i32_mul)
   BINOP(i32_div_s)
   BINOP(i32_div_u)
   BINOP(i32_rem_s)
   BINOP(i32_rem_u)
   BINOP(i32_and)
   BINOP(i32_or)
   BINOP(i32_xor)
   BINOP(i32_shl)
   BINOP(i32_shr_s)
   BINOP(i32_shr_u)
   BINOP(i32_rotl)
   BINOP(i32_rotr)
   UNOP(i64_clz)
   UNOP(i64_ctz)
   UNOP(i64_popcnt)
   BINOP(i64_add)
   BINOP(i64_sub)
   BINOP(i64_mul)
   BINOP(i64_div_s)
   BINOP(i64_div_u)
   BINOP(i64_rem_s)
   BINOP(i64_rem_u)
   BINOP(i64_and)
   BINOP(i64_or)
   BINOP(i64_xor)
   BINOP(i64_shl)
   BINOP(i64_shr_s)
   BINOP(i64_shr_u)
   BINOP(i64_rotl)
   BINOP(i64_rotr)

   UNOP(f32_abs)
   UNOP(f32_neg)
   UNOP(f32_ceil)
   UNOP(f32_floor)
   UNOP(f32_trunc)
   UNOP(f32_nearest)
   UNOP(f32_sqrt)
   BINOP(f32_add)
   BINOP(f32_sub)
   BINOP(f32_mul)
   BINOP(f32_div)
   BINOP(f32_min)
   BINOP(f32_max)
   BINOP(f32_copysign)
   UNOP(f64_abs)
   UNOP(f64_neg)
   UNOP(f64_ceil)
   UNOP(f64_floor)
   UNOP(f64_trunc)
   UNOP(f64_nearest)
   UNOP(f64_sqrt)
   BINOP(f64_add)
   BINOP(f64_sub)
   BINOP(f64_mul)
   BINOP(f64_div)
   BINOP(f64_min)
   BINOP(f64_max)
   BINOP(f64_copysign)

   UNOP(i32_wrap_i64)
   UNOP(i32_trunc_s_f32)
   UNOP(i32_trunc_u_f32)
   UNOP(i32_trunc_s_f64)
   UNOP(i32_trunc_u_f64)
   UNOP(i64_extend_s_i32)
   UNOP(i64_extend_u_i32)
   UNOP(i64_trunc_s_f32)
   UNOP(i64_trunc_u_f32)
   UNOP(i64_trunc_s_f64)
   UNOP(i64_trunc_u_f64)
   UNOP(f32_convert_s_i32)
   UNOP(f32_convert_u_i32)
   UNOP(f32_convert_s_i64)
   UNOP(f32_convert_u_i64)
   UNOP(f32_demote_f64)
   UNOP(f64_convert_s_i32)
   UNOP(f64_convert_u_i32)
   UNOP(f64_convert_s_i64)
   UNOP(f64_convert_u_i64)
   UNOP(f64_promote_f32)
   UNOP(i32_reinterpret_f32)
   UNOP(i64_reinterpret_f64)
   UNOP(f32_reinterpret_i32)
   UNOP(f64_reinterpret_i64)

#undef BINOP
#undef UNOP
#undef OP

   void emit_error() {
      fb[op_index++] = error_t{};
   }

   void fix_branch(uint32_t* branch, uint32_t target) {
      if (branch)
         *branch = _base_offset + target;
   }
   void emit_prologue(const func_type& ft, const guarded_vector<local_entry>&, uint32_t idx) {
      op_index = 0;
      // pre-allocate for the function body code, so we have a big blob of memory to work with during function code
      // parsing
      fb = guarded_vector<opcode>{_allocator, _mod->code[idx].size};
   }
   void emit_epilogue(const func_type& ft, const guarded_vector<local_entry>& locals, uint32_t idx) {
      fb.resize(op_index + 1);
      uint32_t locals_count = 0;
      for (uint32_t i = 0; i < locals.size(); ++i) {
         locals_count += locals[i].count;
      }
      fb[fb.size() - 1] = return_t{static_cast<uint32_t>(locals_count + ft.param_types.size()), ft.return_count, 0, 0};
   }

   void finalize(function_body& body) {
      op_index++;
      fb.resize(op_index);
      body.code = fb.raw();
      body.size = op_index;
      _base_offset += body.size;
   }

   const void* get_addr() const {
      return fb.raw() + op_index;
   }
   const void* get_base_addr() const {
      return _code_segment_base;
   }

 private:
   growable_allocator& _allocator;
   void* _code_segment_base;
   std::size_t op_index = 0;
   guarded_vector<opcode> fb;
   module* _mod;
   std::size_t _base_offset = 0;
};

} // namespace forge::vm::wasm

namespace forge::vm::wasm {

// create constexpr flags for whether the backend should obey alignment hints
#ifdef FORGE_VM_WASM_ALIGN_MEMORY_OPS
inline constexpr bool should_align_memory_ops = true;
#else
inline constexpr bool should_align_memory_ops = false;
#endif

inline constexpr bool use_softfloat = true;

#ifdef FORGE_VM_WASM_FULL_DEBUG
inline constexpr bool wasm_debug = true;
#else
inline constexpr bool wasm_debug = false;
#endif

#ifdef __x86_64__
inline constexpr bool wasm_amd64 = true;
#else
inline constexpr bool wasm_amd64 = false;
#endif

} // namespace forge::vm::wasm

namespace forge::vm::wasm {

struct base_visitor {
   [[gnu::always_inline]] inline void operator()(const unreachable_t&) {}
   [[gnu::always_inline]] inline void operator()(const nop_t&) {}
   [[gnu::always_inline]] inline void operator()(const exit_t&) {}
   [[gnu::always_inline]] inline void operator()(const end_t&) {}
   [[gnu::always_inline]] inline void operator()(const return_t&) {}
   [[gnu::always_inline]] inline void operator()(const block_t&) {}
   [[gnu::always_inline]] inline void operator()(const loop_t&) {}
   [[gnu::always_inline]] inline void operator()(const if_t&) {}
   [[gnu::always_inline]] inline void operator()(const else_t&) {}
   [[gnu::always_inline]] inline void operator()(const br_t&) {}
   [[gnu::always_inline]] inline void operator()(const br_if_t&) {}
   [[gnu::always_inline]] inline void operator()(const br_table_t&) {}
   [[gnu::always_inline]] inline void operator()(const call_t&) {}
   [[gnu::always_inline]] inline void operator()(const call_indirect_t&) {}
   [[gnu::always_inline]] inline void operator()(const drop_t&) {}
   [[gnu::always_inline]] inline void operator()(const select_t&) {}
   [[gnu::always_inline]] inline void operator()(const get_local_t&) {}
   [[gnu::always_inline]] inline void operator()(const set_local_t&) {}
   [[gnu::always_inline]] inline void operator()(const tee_local_t&) {}
   [[gnu::always_inline]] inline void operator()(const get_global_t&) {}
   [[gnu::always_inline]] inline void operator()(const set_global_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_load_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_load8_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_load16_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_load8_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_load16_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_load_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_load8_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_load16_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_load32_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_load8_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_load16_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_load32_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_load_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_load_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_store_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_store8_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_store16_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_store_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_store8_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_store16_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_store32_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_store_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_store_t&) {}
   [[gnu::always_inline]] inline void operator()(const current_memory_t&) {}
   [[gnu::always_inline]] inline void operator()(const grow_memory_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_const_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_const_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_const_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_const_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_eqz_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_eq_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_ne_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_lt_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_lt_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_le_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_le_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_gt_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_gt_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_ge_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_ge_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_eqz_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_eq_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_ne_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_lt_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_lt_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_le_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_le_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_gt_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_gt_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_ge_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_ge_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_eq_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_ne_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_lt_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_gt_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_le_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_ge_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_eq_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_ne_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_lt_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_gt_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_le_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_ge_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_clz_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_ctz_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_popcnt_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_add_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_sub_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_mul_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_div_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_div_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_rem_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_rem_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_and_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_or_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_xor_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_shl_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_shr_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_shr_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_rotl_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_rotr_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_clz_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_ctz_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_popcnt_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_add_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_sub_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_mul_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_div_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_div_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_rem_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_rem_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_and_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_or_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_xor_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_shl_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_shr_s_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_shr_u_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_rotl_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_rotr_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_abs_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_neg_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_ceil_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_floor_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_trunc_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_nearest_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_sqrt_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_add_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_sub_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_mul_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_div_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_min_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_max_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_copysign_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_abs_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_neg_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_ceil_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_floor_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_trunc_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_nearest_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_sqrt_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_add_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_sub_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_mul_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_div_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_min_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_max_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_copysign_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_wrap_i64_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_trunc_s_f32_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_trunc_u_f32_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_trunc_s_f64_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_trunc_u_f64_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_extend_s_i32_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_extend_u_i32_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_trunc_s_f32_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_trunc_u_f32_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_trunc_s_f64_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_trunc_u_f64_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_convert_s_i32_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_convert_u_i32_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_convert_s_i64_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_convert_u_i64_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_demote_f64_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_convert_s_i32_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_convert_u_i32_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_convert_s_i64_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_convert_u_i64_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_promote_f32_t&) {}
   [[gnu::always_inline]] inline void operator()(const i32_reinterpret_f32_t&) {}
   [[gnu::always_inline]] inline void operator()(const i64_reinterpret_f64_t&) {}
   [[gnu::always_inline]] inline void operator()(const f32_reinterpret_i32_t&) {}
   [[gnu::always_inline]] inline void operator()(const f64_reinterpret_i64_t&) {}
   template <typename T> [[gnu::always_inline]] inline void operator()(T val) {
      throw exceptions::interpreter{"invalid opcode"};
   }
};

} // namespace forge::vm::wasm

namespace forge::vm::wasm {
inline constexpr uint32_t inv_float_eps = 0x4B000000;
inline constexpr uint64_t inv_double_eps = 0x4330000000000000;

inline bool is_nan(const float32_t f) {
   return f32_is_nan(f);
}
inline bool is_nan(const float64_t f) {
   return f64_is_nan(f);
}

inline float _wasm_f32_add(float a, float b) {
   return ::from_softfloat32(::f32_add(::to_softfloat32(a), ::to_softfloat32(b)));
}

inline float _wasm_f32_sub(float a, float b) {
   return ::from_softfloat32(::f32_sub(::to_softfloat32(a), ::to_softfloat32(b)));
}

inline float _wasm_f32_div(float a, float b) {
   return ::from_softfloat32(::f32_div(::to_softfloat32(a), ::to_softfloat32(b)));
}

inline float _wasm_f32_mul(float a, float b) {
   return ::from_softfloat32(::f32_mul(::to_softfloat32(a), ::to_softfloat32(b)));
}

inline float _wasm_f32_min(float af, float bf) {
   float32_t a = to_softfloat32(af);
   float32_t b = to_softfloat32(bf);
   if (is_nan(a)) {
      return af;
   }
   if (is_nan(b)) {
      return bf;
   }
   if (f32_sign_bit(a) != f32_sign_bit(b)) {
      return f32_sign_bit(a) ? af : bf;
   }
   return ::f32_lt(a, b) ? af : bf;
}

inline float _wasm_f32_max(float af, float bf) {
   float32_t a = to_softfloat32(af);
   float32_t b = to_softfloat32(bf);
   if (is_nan(a)) {
      return af;
   }
   if (is_nan(b)) {
      return bf;
   }
   if (f32_sign_bit(a) != f32_sign_bit(b)) {
      return f32_sign_bit(a) ? bf : af;
   }
   return ::f32_lt(a, b) ? bf : af;
}

inline float _wasm_f32_copysign(float af, float bf) {
   float32_t a = to_softfloat32(af);
   float32_t b = to_softfloat32(bf);
   a.v &= ~(1 << 31);               // clear the sign bit
   a.v = a.v | ((b.v >> 31) << 31); // add the sign of b
   return from_softfloat32(a);
}

inline float _wasm_f32_abs(float af) {
   float32_t a = to_softfloat32(af);
   a.v &= ~(1 << 31);
   return from_softfloat32(a);
}

inline float _wasm_f32_neg(float af) {
   float32_t a = to_softfloat32(af);
   uint32_t sign = a.v >> 31;
   a.v &= ~(1 << 31);
   a.v |= (!sign << 31);
   return from_softfloat32(a);
}

inline float _wasm_f32_sqrt(float a) {
   float32_t ret = ::f32_sqrt(to_softfloat32(a));
   return from_softfloat32(ret);
}

inline float _wasm_f32_ceil(float af) {
   float32_t a = to_softfloat32(af);
   int e = (int)(a.v >> 23 & 0xFF) - 0X7F;
   uint32_t m;
   if (e >= 23)
      return af;
   if (e >= 0) {
      m = 0x007FFFFF >> e;
      if ((a.v & m) == 0)
         return af;
      if (a.v >> 31 == 0)
         a.v += m;
      a.v &= ~m;
   } else {
      if (a.v >> 31)
         a.v = 0x80000000; // return -0.0f
      else if (a.v << 1)
         a.v = 0x3F800000; // return 1.0f
   }

   return from_softfloat32(a);
}

inline float _wasm_f32_floor(float af) {
   float32_t a = to_softfloat32(af);
   int e = (int)(a.v >> 23 & 0xFF) - 0X7F;
   uint32_t m;
   if (e >= 23)
      return af;
   if (e >= 0) {
      m = 0x007FFFFF >> e;
      if ((a.v & m) == 0)
         return af;
      if (a.v >> 31)
         a.v += m;
      a.v &= ~m;
   } else {
      if (a.v >> 31 == 0)
         a.v = 0;
      else if (a.v << 1)
         a.v = 0xBF800000; // return -1.0f
   }
   return from_softfloat32(a);
}

inline float _wasm_f32_trunc(float af) {
   float32_t a = to_softfloat32(af);
   int e = (int)(a.v >> 23 & 0xff) - 0x7f + 9;
   uint32_t m;
   if (e >= 23 + 9)
      return af;
   if (e < 9)
      e = 1;
   m = -1U >> e;
   if ((a.v & m) == 0)
      return af;
   a.v &= ~m;
   return from_softfloat32(a);
}

inline float _wasm_f32_nearest(float af) {
   float32_t a = to_softfloat32(af);
   int e = a.v >> 23 & 0xff;
   int s = a.v >> 31;
   float32_t y;
   if (e >= 0x7f + 23)
      return af;
   if (s)
      y = ::f32_add(::f32_sub(a, float32_t{inv_float_eps}), float32_t{inv_float_eps});
   else
      y = ::f32_sub(::f32_add(a, float32_t{inv_float_eps}), float32_t{inv_float_eps});
   if (::f32_eq(y, {0}))
      return s ? -0.0f : 0.0f;
   return from_softfloat32(y);
}

inline bool _wasm_f32_eq(float a, float b) {
   return ::f32_eq(to_softfloat32(a), to_softfloat32(b));
}
inline bool _wasm_f32_ne(float a, float b) {
   return !::f32_eq(to_softfloat32(a), to_softfloat32(b));
}
inline bool _wasm_f32_lt(float a, float b) {
   return ::f32_lt(to_softfloat32(a), to_softfloat32(b));
}
inline bool _wasm_f32_le(float a, float b) {
   return ::f32_le(to_softfloat32(a), to_softfloat32(b));
}
inline bool _wasm_f32_gt(float af, float bf) {
   float32_t a = to_softfloat32(af);
   float32_t b = to_softfloat32(bf);
   if (is_nan(a))
      return false;
   if (is_nan(b))
      return false;
   return !::f32_le(a, b);
}

inline bool _wasm_f32_ge(float af, float bf) {
   float32_t a = to_softfloat32(af);
   float32_t b = to_softfloat32(bf);
   if (is_nan(a))
      return false;
   if (is_nan(b))
      return false;
   return !::f32_lt(a, b);
}

inline double _wasm_f64_add(double a, double b) {
   float64_t ret = ::f64_add(to_softfloat64(a), to_softfloat64(b));
   return from_softfloat64(ret);
}

inline double _wasm_f64_sub(double a, double b) {
   float64_t ret = ::f64_sub(to_softfloat64(a), to_softfloat64(b));
   return from_softfloat64(ret);
}

inline double _wasm_f64_div(double a, double b) {
   float64_t ret = ::f64_div(to_softfloat64(a), to_softfloat64(b));
   return from_softfloat64(ret);
}

inline double _wasm_f64_mul(double a, double b) {
   float64_t ret = ::f64_mul(to_softfloat64(a), to_softfloat64(b));
   return from_softfloat64(ret);
}

inline double _wasm_f64_min(double af, double bf) {
   float64_t a = to_softfloat64(af);
   float64_t b = to_softfloat64(bf);
   if (is_nan(a))
      return af;
   if (is_nan(b))
      return bf;
   if (f64_sign_bit(a) != f64_sign_bit(b))
      return f64_sign_bit(a) ? af : bf;
   return ::f64_lt(a, b) ? af : bf;
}

inline double _wasm_f64_max(double af, double bf) {
   float64_t a = to_softfloat64(af);
   float64_t b = to_softfloat64(bf);
   if (is_nan(a))
      return af;
   if (is_nan(b))
      return bf;
   if (f64_sign_bit(a) != f64_sign_bit(b))
      return f64_sign_bit(a) ? bf : af;
   return ::f64_lt(a, b) ? bf : af;
}

inline double _wasm_f64_copysign(double af, double bf) {
   float64_t a = to_softfloat64(af);
   float64_t b = to_softfloat64(bf);
   a.v &= ~(uint64_t(1) << 63);     // clear the sign bit
   a.v = a.v | ((b.v >> 63) << 63); // add the sign of b
   return from_softfloat64(a);
}

inline double _wasm_f64_abs(double af) {
   float64_t a = to_softfloat64(af);
   a.v &= ~(uint64_t(1) << 63);
   return from_softfloat64(a);
}

inline double _wasm_f64_neg(double af) {
   float64_t a = to_softfloat64(af);
   uint64_t sign = a.v >> 63;
   a.v &= ~(uint64_t(1) << 63);
   a.v |= (uint64_t(!sign) << 63);
   return from_softfloat64(a);
}

inline double _wasm_f64_sqrt(double a) {
   float64_t ret = ::f64_sqrt(to_softfloat64(a));
   return from_softfloat64(ret);
}

inline double _wasm_f64_ceil(double af) {
   float64_t a = to_softfloat64(af);
   float64_t ret;
   int e = a.v >> 52 & 0x7ff;
   float64_t y;
   if (e >= 0x3ff + 52 || ::f64_eq(a, {0}))
      return af;
   /* y = int(x) - x, where int(x) is an integer neighbor of x */
   if (a.v >> 63)
      y = ::f64_sub(::f64_add(::f64_sub(a, float64_t{inv_double_eps}), float64_t{inv_double_eps}), a);
   else
      y = ::f64_sub(::f64_sub(::f64_add(a, float64_t{inv_double_eps}), float64_t{inv_double_eps}), a);
   /* special case because of non-nearest rounding modes */
   if (e <= 0x3ff - 1) {
      return a.v >> 63 ? -0.0 : 1.0; // float64_t{0x8000000000000000} : float64_t{0xBE99999A3F800000}; //either -0.0 or
                                     // 1
   }
   if (::f64_lt(y, to_softfloat64(0))) {
      ret = ::f64_add(::f64_add(a, y), to_softfloat64(1)); // 0xBE99999A3F800000 } ); // plus 1
      return from_softfloat64(ret);
   }
   ret = ::f64_add(a, y);
   return from_softfloat64(ret);
}

inline double _wasm_f64_floor(double af) {
   float64_t a = to_softfloat64(af);
   float64_t ret;
   int e = a.v >> 52 & 0x7FF;
   float64_t y;
   if (a.v == 0x8000000000000000) {
      return af;
   }
   if (e >= 0x3FF + 52 || a.v == 0) {
      return af;
   }
   if (a.v >> 63)
      y = ::f64_sub(::f64_add(::f64_sub(a, float64_t{inv_double_eps}), float64_t{inv_double_eps}), a);
   else
      y = ::f64_sub(::f64_sub(::f64_add(a, float64_t{inv_double_eps}), float64_t{inv_double_eps}), a);
   if (e <= 0x3FF - 1) {
      return a.v >> 63 ? -1.0 : 0.0; // float64_t{0xBFF0000000000000} : float64_t{0}; // -1 or 0
   }
   if (!::f64_le(y, float64_t{0})) {
      ret = ::f64_sub(::f64_add(a, y), to_softfloat64(1.0));
      return from_softfloat64(ret);
   }
   ret = ::f64_add(a, y);
   return from_softfloat64(ret);
}

inline double _wasm_f64_trunc(double af) {
   float64_t a = to_softfloat64(af);
   int e = (int)(a.v >> 52 & 0x7ff) - 0x3ff + 12;
   uint64_t m;
   if (e >= 52 + 12)
      return af;
   if (e < 12)
      e = 1;
   m = -1ULL >> e;
   if ((a.v & m) == 0)
      return af;
   a.v &= ~m;
   return from_softfloat64(a);
}

inline double _wasm_f64_nearest(double af) {
   float64_t a = to_softfloat64(af);
   int e = (a.v >> 52 & 0x7FF);
   int s = a.v >> 63;
   float64_t y;
   if (e >= 0x3FF + 52)
      return af;
   if (s)
      y = ::f64_add(::f64_sub(a, float64_t{inv_double_eps}), float64_t{inv_double_eps});
   else
      y = ::f64_sub(::f64_add(a, float64_t{inv_double_eps}), float64_t{inv_double_eps});
   if (::f64_eq(y, float64_t{0}))
      return s ? -0.0 : 0.0;
   return from_softfloat64(y);
}

inline bool _wasm_f64_eq(double a, double b) {
   return ::f64_eq(to_softfloat64(a), to_softfloat64(b));
}
inline bool _wasm_f64_ne(double a, double b) {
   return !::f64_eq(to_softfloat64(a), to_softfloat64(b));
}
inline bool _wasm_f64_lt(double a, double b) {
   return ::f64_lt(to_softfloat64(a), to_softfloat64(b));
}
inline bool _wasm_f64_le(double a, double b) {
   return ::f64_le(to_softfloat64(a), to_softfloat64(b));
}
inline bool _wasm_f64_gt(double af, double bf) {
   float64_t a = to_softfloat64(af);
   float64_t b = to_softfloat64(bf);
   if (is_nan(a))
      return false;
   if (is_nan(b))
      return false;
   return !::f64_le(a, b);
}

inline bool _wasm_f64_ge(double af, double bf) {
   float64_t a = to_softfloat64(af);
   float64_t b = to_softfloat64(bf);
   if (is_nan(a))
      return false;
   if (is_nan(b))
      return false;
   return !::f64_lt(a, b);
}

inline double _wasm_f32_promote(float a) {
   return from_softfloat64(f32_to_f64(to_softfloat32(a)));
}

inline float _wasm_f64_demote(double a) {
   return from_softfloat32(f64_to_f32(to_softfloat64(a)));
}

inline int32_t _wasm_f32_trunc_i32s(float af) {
   float32_t a = to_softfloat32(af);
   detail::check<exceptions::interpreter>((!(_wasm_f32_ge(af, 2147483648.0f) || _wasm_f32_lt(af, -2147483648.0f))),
                                          "Error, f32.convert_s/i32 overflow");

   detail::check<exceptions::interpreter>((!is_nan(a)), "Error, f32.convert_s/i32 unrepresentable");
   return f32_to_i32(to_softfloat32(_wasm_f32_trunc(af)), 0, false);
}

inline int32_t _wasm_f64_trunc_i32s(double af) {
   float64_t a = to_softfloat64(af);
   detail::check<exceptions::interpreter>((!(_wasm_f64_ge(af, 2147483648.0) || _wasm_f64_lt(af, -2147483648.0))),
                                          "Error, f64.convert_s/i32 overflow");
   detail::check<exceptions::interpreter>((!is_nan(a)), "Error, f64.convert_s/i32 unrepresentable");
   return f64_to_i32(to_softfloat64(_wasm_f64_trunc(af)), 0, false);
}

inline uint32_t _wasm_f32_trunc_i32u(float af) {
   float32_t a = to_softfloat32(af);
   detail::check<exceptions::interpreter>((!(_wasm_f32_ge(af, 4294967296.0f) || _wasm_f32_le(af, -1.0f))),
                                          "Error, f32.convert_u/i32 overflow");
   detail::check<exceptions::interpreter>((!is_nan(a)), "Error, f32.convert_u/i32 unrepresentable");
   return f32_to_ui32(to_softfloat32(_wasm_f32_trunc(af)), 0, false);
}

inline uint32_t _wasm_f64_trunc_i32u(double af) {
   float64_t a = to_softfloat64(af);
   detail::check<exceptions::interpreter>((!(_wasm_f64_ge(af, 4294967296.0) || _wasm_f64_le(af, -1.0))),
                                          "Error, f64.convert_u/i32 overflow");
   detail::check<exceptions::interpreter>((!is_nan(a)), "Error, f64.convert_u/i32 unrepresentable");
   return f64_to_ui32(to_softfloat64(_wasm_f64_trunc(af)), 0, false);
}

inline int64_t _wasm_f32_trunc_i64s(float af) {
   float32_t a = to_softfloat32(af);
   detail::check<exceptions::interpreter>(
       (!(_wasm_f32_ge(af, 9223372036854775808.0f) || _wasm_f32_lt(af, -9223372036854775808.0f))),
       "Error, f32.convert_s/i64 overflow");
   detail::check<exceptions::interpreter>((!is_nan(a)), "Error, f32.convert_s/i64 unrepresentable");
   return f32_to_i64(to_softfloat32(_wasm_f32_trunc(af)), 0, false);
}

inline int64_t _wasm_f64_trunc_i64s(double af) {
   float64_t a = to_softfloat64(af);
   detail::check<exceptions::interpreter>(
       (!(_wasm_f64_ge(af, 9223372036854775808.0) || _wasm_f64_lt(af, -9223372036854775808.0))),
       "Error, f64.convert_s/i64 overflow");
   detail::check<exceptions::interpreter>((!is_nan(a)), "Error, f64.convert_s/i64 unrepresentable");

   return f64_to_i64(to_softfloat64(_wasm_f64_trunc(af)), 0, false);
}

inline uint64_t _wasm_f32_trunc_i64u(float af) {
   float32_t a = to_softfloat32(af);
   detail::check<exceptions::interpreter>((!(_wasm_f32_ge(af, 18446744073709551616.0f) || _wasm_f32_le(af, -1.0f))),
                                          "Error, f32.convert_u/i64 overflow");
   detail::check<exceptions::interpreter>((!is_nan(a)), "Error, f32.convert_u/i64 unrepresentable");
   return f32_to_ui64(to_softfloat32(_wasm_f32_trunc(af)), 0, false);
}

inline uint64_t _wasm_f64_trunc_i64u(double af) {
   float64_t a = to_softfloat64(af);
   detail::check<exceptions::interpreter>((!(_wasm_f64_ge(af, 18446744073709551616.0) || _wasm_f64_le(af, -1.0))),
                                          "Error, f64.convert_u/i64 overflow");
   detail::check<exceptions::interpreter>((!is_nan(a)), "Error, f64.convert_u/i64 unrepresentable");
   return f64_to_ui64(to_softfloat64(_wasm_f64_trunc(af)), 0, false);
}

inline float _wasm_i32_to_f32(int32_t a) {
   return from_softfloat32(i32_to_f32(a));
}

inline float _wasm_i64_to_f32(int64_t a) {
   return from_softfloat32(i64_to_f32(a));
}

inline float _wasm_ui32_to_f32(uint32_t a) {
   return from_softfloat32(ui32_to_f32(a));
}

inline float _wasm_ui64_to_f32(uint64_t a) {
   return from_softfloat32(ui64_to_f32(a));
}

inline double _wasm_i32_to_f64(int32_t a) {
   return from_softfloat64(i32_to_f64(a));
}

inline double _wasm_i64_to_f64(int64_t a) {
   return from_softfloat64(i64_to_f64(a));
}

inline double _wasm_ui32_to_f64(uint32_t a) {
   return from_softfloat64(ui32_to_f64(a));
}

inline double _wasm_ui64_to_f64(uint64_t a) {
   return from_softfloat64(ui64_to_f64(a));
}
} // namespace forge::vm::wasm

namespace forge::vm::wasm {

template <typename ExecutionContext> struct interpret_visitor : base_visitor {
   using base_visitor::operator();
   interpret_visitor(ExecutionContext& ec) : context(ec) {}
   ExecutionContext& context;

   ExecutionContext& get_context() {
      return context;
   }

   static inline constexpr void* align_address(void* addr, size_t align_amt) {
      if constexpr (should_align_memory_ops) {
         addr = (void*)(((uintptr_t)addr + (1 << align_amt) - 1) & ~((1 << align_amt) - 1));
         return addr;
      } else {
         return addr;
      }
   }
   template <typename T> static inline T read_unaligned(const void* addr) {
      T result;
      std::memcpy(&result, addr, sizeof(T));
      return result;
   }
   template <typename T> static void write_unaligned(void* addr, T value) {
      std::memcpy(addr, &value, sizeof(T));
   }

   [[gnu::always_inline]] inline void operator()(const unreachable_t& op) {
      context.inc_pc();
      throw exceptions::interpreter{"unreachable"};
   }

   [[gnu::always_inline]] inline void operator()(const nop_t& op) {
      context.inc_pc();
   }

   [[gnu::always_inline]] inline void operator()(const end_t& op) {
      context.inc_pc();
   }
   [[gnu::always_inline]] inline void operator()(const return_t& op) {
      context.apply_pop_call(op.data, op.pc);
   }
   [[gnu::always_inline]] inline void operator()(const block_t& op) {
      context.inc_pc();
   }
   [[gnu::always_inline]] inline void operator()(const loop_t& op) {
      context.inc_pc();
   }
   [[gnu::always_inline]] inline void operator()(const if_t& op) {
      context.inc_pc();
      const auto& oper = context.pop_operand();
      if (!oper.to_ui32()) {
         context.set_relative_pc(op.pc);
      }
   }
   [[gnu::always_inline]] inline void operator()(const else_t& op) {
      context.set_relative_pc(op.pc);
   }
   [[gnu::always_inline]] inline void operator()(const br_t& op) {
      context.jump(op.data, op.pc);
   }
   [[gnu::always_inline]] inline void operator()(const br_if_t& op) {
      const auto& val = context.pop_operand();
      if (context.is_true(val)) {
         context.jump(op.data, op.pc);
      } else {
         context.inc_pc();
      }
   }

   [[gnu::always_inline]] inline void operator()(const br_table_data_t& op) {
      context.inc_pc(op.index);
   }
   [[gnu::always_inline]] inline void operator()(const br_table_t& op) {
      const auto& in = context.pop_operand().to_ui32();
      const auto& entry = op.table[std::min(in, op.size)];
      context.jump(entry.stack_pop, entry.pc);
   }
   [[gnu::always_inline]] inline void operator()(const call_t& op) {
      context.call(op.index);
   }
   [[gnu::always_inline]] inline void operator()(const call_indirect_t& op) {
      const auto& index = context.pop_operand().to_ui32();
      uint32_t fn = context.table_elem(index);
      const auto& expected_type = context.get_module().types.at(op.index);
      const auto& actual_type = context.get_module().get_function_type(fn);
      detail::check<exceptions::interpreter>((actual_type == expected_type), "bad call_indirect type");
      context.call(fn);
   }
   [[gnu::always_inline]] inline void operator()(const drop_t& op) {
      context.pop_operand();
      context.inc_pc();
   }
   [[gnu::always_inline]] inline void operator()(const select_t& op) {
      const auto& c = context.pop_operand();
      const auto& v2 = context.pop_operand();
      if (c.to_ui32() == 0) {
         context.peek_operand() = v2;
      }
      context.inc_pc();
   }
   [[gnu::always_inline]] inline void operator()(const get_local_t& op) {
      context.inc_pc();
      context.push_operand(context.get_operand(op.index));
   }
   [[gnu::always_inline]] inline void operator()(const set_local_t& op) {
      context.inc_pc();
      context.set_operand(op.index, context.pop_operand());
   }
   [[gnu::always_inline]] inline void operator()(const tee_local_t& op) {
      context.inc_pc();
      const auto& oper = context.pop_operand();
      context.set_operand(op.index, oper);
      context.push_operand(oper);
   }
   [[gnu::always_inline]] inline void operator()(const get_global_t& op) {
      context.inc_pc();
      const auto& gl = context.get_global(op.index);
      context.push_operand(gl);
   }
   [[gnu::always_inline]] inline void operator()(const set_global_t& op) {
      context.inc_pc();
      const auto& oper = context.pop_operand();
      context.set_global(op.index, oper);
   }
   template <typename Op> inline void* pop_memop_addr(const Op& op) {
      const auto& ptr = context.pop_operand();
      return align_address((context.linear_memory() + op.offset + ptr.to_ui32()), op.flags_align);
   }
   [[gnu::always_inline]] inline void operator()(const i32_load_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i32_const_t{read_unaligned<uint32_t>(_ptr)});
   }
   [[gnu::always_inline]] inline void operator()(const i32_load8_s_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i32_const_t{static_cast<int32_t>(read_unaligned<int8_t>(_ptr))});
   }
   [[gnu::always_inline]] inline void operator()(const i32_load16_s_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i32_const_t{static_cast<int32_t>(read_unaligned<int16_t>(_ptr))});
   }
   [[gnu::always_inline]] inline void operator()(const i32_load8_u_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i32_const_t{static_cast<uint32_t>(read_unaligned<uint8_t>(_ptr))});
   }
   [[gnu::always_inline]] inline void operator()(const i32_load16_u_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i32_const_t{static_cast<uint32_t>(read_unaligned<uint16_t>(_ptr))});
   }
   [[gnu::always_inline]] inline void operator()(const i64_load_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i64_const_t{static_cast<uint64_t>(read_unaligned<uint64_t>(_ptr))});
   }
   [[gnu::always_inline]] inline void operator()(const i64_load8_s_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i64_const_t{static_cast<int64_t>(read_unaligned<int8_t>(_ptr))});
   }
   [[gnu::always_inline]] inline void operator()(const i64_load16_s_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i64_const_t{static_cast<int64_t>(read_unaligned<int16_t>(_ptr))});
   }
   [[gnu::always_inline]] inline void operator()(const i64_load32_s_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i64_const_t{static_cast<int64_t>(read_unaligned<int32_t>(_ptr))});
   }
   [[gnu::always_inline]] inline void operator()(const i64_load8_u_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i64_const_t{static_cast<uint64_t>(read_unaligned<uint8_t>(_ptr))});
   }
   [[gnu::always_inline]] inline void operator()(const i64_load16_u_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i64_const_t{static_cast<uint64_t>(read_unaligned<uint16_t>(_ptr))});
   }
   [[gnu::always_inline]] inline void operator()(const i64_load32_u_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(i64_const_t{static_cast<uint64_t>(read_unaligned<uint32_t>(_ptr))});
   }
   [[gnu::always_inline]] inline void operator()(const f32_load_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(f32_const_t{read_unaligned<uint32_t>(_ptr)});
   }
   [[gnu::always_inline]] inline void operator()(const f64_load_t& op) {
      context.inc_pc();
      void* _ptr = pop_memop_addr(op);
      context.push_operand(f64_const_t{read_unaligned<uint64_t>(_ptr)});
   }
   [[gnu::always_inline]] inline void operator()(const i32_store_t& op) {
      context.inc_pc();
      const auto& val = context.pop_operand();
      void* store_loc = pop_memop_addr(op);
      write_unaligned(store_loc, val.to_ui32());
   }
   [[gnu::always_inline]] inline void operator()(const i32_store8_t& op) {
      context.inc_pc();
      const auto& val = context.pop_operand();
      void* store_loc = pop_memop_addr(op);
      write_unaligned(store_loc, static_cast<uint8_t>(val.to_ui32()));
   }
   [[gnu::always_inline]] inline void operator()(const i32_store16_t& op) {
      context.inc_pc();
      const auto& val = context.pop_operand();
      void* store_loc = pop_memop_addr(op);
      write_unaligned(store_loc, static_cast<uint16_t>(val.to_ui32()));
   }
   [[gnu::always_inline]] inline void operator()(const i64_store_t& op) {
      context.inc_pc();
      const auto& val = context.pop_operand();
      void* store_loc = pop_memop_addr(op);
      write_unaligned(store_loc, static_cast<uint64_t>(val.to_ui64()));
   }
   [[gnu::always_inline]] inline void operator()(const i64_store8_t& op) {
      context.inc_pc();
      const auto& val = context.pop_operand();
      void* store_loc = pop_memop_addr(op);
      write_unaligned(store_loc, static_cast<uint8_t>(val.to_ui64()));
   }
   [[gnu::always_inline]] inline void operator()(const i64_store16_t& op) {
      context.inc_pc();
      const auto& val = context.pop_operand();
      void* store_loc = pop_memop_addr(op);
      write_unaligned(store_loc, static_cast<uint16_t>(val.to_ui64()));
   }
   [[gnu::always_inline]] inline void operator()(const i64_store32_t& op) {
      context.inc_pc();
      const auto& val = context.pop_operand();
      void* store_loc = pop_memop_addr(op);
      write_unaligned(store_loc, static_cast<uint32_t>(val.to_ui64()));
   }
   [[gnu::always_inline]] inline void operator()(const f32_store_t& op) {
      context.inc_pc();
      const auto& val = context.pop_operand();
      void* store_loc = pop_memop_addr(op);
      write_unaligned(store_loc, static_cast<uint32_t>(val.to_fui32()));
   }
   [[gnu::always_inline]] inline void operator()(const f64_store_t& op) {
      context.inc_pc();
      const auto& val = context.pop_operand();
      void* store_loc = pop_memop_addr(op);
      write_unaligned(store_loc, static_cast<uint64_t>(val.to_fui64()));
   }
   [[gnu::always_inline]] inline void operator()(const current_memory_t& op) {
      context.inc_pc();
      context.push_operand(i32_const_t{context.current_linear_memory()});
   }
   [[gnu::always_inline]] inline void operator()(const grow_memory_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_ui32();
      oper = context.grow_linear_memory(oper);
   }
   [[gnu::always_inline]] inline void operator()(const i32_const_t& op) {
      context.inc_pc();
      context.push_operand(op);
   }
   [[gnu::always_inline]] inline void operator()(const i64_const_t& op) {
      context.inc_pc();
      context.push_operand(op);
   }
   [[gnu::always_inline]] inline void operator()(const f32_const_t& op) {
      context.inc_pc();
      context.push_operand(op);
   }
   [[gnu::always_inline]] inline void operator()(const f64_const_t& op) {
      context.inc_pc();
      context.push_operand(op);
   }
   [[gnu::always_inline]] inline void operator()(const i32_eqz_t& op) {
      context.inc_pc();
      auto& t = context.peek_operand().to_ui32();
      t = t == 0;
   }
   [[gnu::always_inline]] inline void operator()(const i32_eq_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs = lhs == rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_ne_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs = lhs != rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_lt_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i32();
      auto& lhs = context.peek_operand().to_i32();
      lhs = lhs < rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_lt_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs = lhs < rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_le_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i32();
      auto& lhs = context.peek_operand().to_i32();
      lhs = lhs <= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_le_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs = lhs <= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_gt_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i32();
      auto& lhs = context.peek_operand().to_i32();
      lhs = lhs > rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_gt_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs = lhs > rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_ge_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i32();
      auto& lhs = context.peek_operand().to_i32();
      lhs = lhs >= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_ge_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs = lhs >= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i64_eqz_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      oper = i32_const_t{oper.to_ui64() == 0};
   }
   [[gnu::always_inline]] inline void operator()(const i64_eq_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand();
      lhs = i32_const_t{lhs.to_ui64() == rhs};
   }
   [[gnu::always_inline]] inline void operator()(const i64_ne_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand();
      lhs = i32_const_t{lhs.to_ui64() != rhs};
   }
   [[gnu::always_inline]] inline void operator()(const i64_lt_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i64();
      auto& lhs = context.peek_operand();
      lhs = i32_const_t{lhs.to_i64() < rhs};
   }
   [[gnu::always_inline]] inline void operator()(const i64_lt_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand();
      lhs = i32_const_t{lhs.to_ui64() < rhs};
   }
   [[gnu::always_inline]] inline void operator()(const i64_le_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i64();
      auto& lhs = context.peek_operand();
      lhs = i32_const_t{lhs.to_i64() <= rhs};
   }
   [[gnu::always_inline]] inline void operator()(const i64_le_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand();
      lhs = i32_const_t{lhs.to_ui64() <= rhs};
   }
   [[gnu::always_inline]] inline void operator()(const i64_gt_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i64();
      auto& lhs = context.peek_operand();
      lhs = i32_const_t{lhs.to_i64() > rhs};
   }
   [[gnu::always_inline]] inline void operator()(const i64_gt_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand();
      lhs = i32_const_t{lhs.to_ui64() > rhs};
   }
   [[gnu::always_inline]] inline void operator()(const i64_ge_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i64();
      auto& lhs = context.peek_operand();
      lhs = i32_const_t{lhs.to_i64() >= rhs};
   }
   [[gnu::always_inline]] inline void operator()(const i64_ge_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand();
      lhs = i32_const_t{lhs.to_ui64() >= rhs};
   }
   [[gnu::always_inline]] inline void operator()(const f32_eq_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f32();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f32_eq(lhs.to_f32(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f32() == rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const f32_ne_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f32();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f32_ne(lhs.to_f32(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f32() != rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const f32_lt_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f32();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f32_lt(lhs.to_f32(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f32() < rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const f32_gt_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f32();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f32_gt(lhs.to_f32(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f32() > rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const f32_le_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f32();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f32_le(lhs.to_f32(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f32() <= rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const f32_ge_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f32();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f32_ge(lhs.to_f32(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f32() >= rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const f64_eq_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f64();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f64_eq(lhs.to_f64(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f64() == rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const f64_ne_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f64();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f64_ne(lhs.to_f64(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f64() != rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const f64_lt_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f64();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f64_lt(lhs.to_f64(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f64() < rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const f64_gt_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f64();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f64_gt(lhs.to_f64(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f64() > rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const f64_le_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f64();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f64_le(lhs.to_f64(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f64() <= rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const f64_ge_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_f64();
      auto& lhs = context.peek_operand();
      if constexpr (use_softfloat)
         lhs = i32_const_t{(uint32_t)_wasm_f64_ge(lhs.to_f64(), rhs)};
      else
         lhs = i32_const_t{(uint32_t)(lhs.to_f64() >= rhs)};
   }
   [[gnu::always_inline]] inline void operator()(const i32_clz_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_ui32();
      // __builtin_clz(0) is undefined
      oper = oper == 0 ? 32 : __builtin_clz(oper);
   }
   [[gnu::always_inline]] inline void operator()(const i32_ctz_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_ui32();

      // __builtin_ctz(0) is undefined
      oper = oper == 0 ? 32 : __builtin_ctz(oper);
   }
   [[gnu::always_inline]] inline void operator()(const i32_popcnt_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_ui32();
      oper = __builtin_popcount(oper);
   }
   [[gnu::always_inline]] inline void operator()(const i32_add_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs += rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_sub_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs -= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_mul_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs *= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_div_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i32();
      auto& lhs = context.peek_operand().to_i32();
      detail::check<exceptions::interpreter>((rhs != 0), "i32.div_s divide by zero");
      detail::check<exceptions::interpreter>((!(lhs == std::numeric_limits<int32_t>::min() && rhs == -1)),
                                             "i32.div_s traps when I32_MAX/-1");
      lhs /= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_div_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      detail::check<exceptions::interpreter>((rhs != 0), "i32.div_u divide by zero");
      lhs /= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_rem_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i32();
      auto& lhs = context.peek_operand().to_i32();
      detail::check<exceptions::interpreter>((rhs != 0), "i32.rem_s divide by zero");
      if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1) [[unlikely]]
         lhs = 0;
      else
         lhs %= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_rem_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      detail::check<exceptions::interpreter>((rhs != 0), "i32.rem_u divide by zero");
      lhs %= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_and_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs &= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_or_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs |= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_xor_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs ^= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i32_shl_t& op) {
      context.inc_pc();
      static constexpr uint32_t mask = (8 * sizeof(uint32_t) - 1);
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs <<= (rhs & mask);
   }
   [[gnu::always_inline]] inline void operator()(const i32_shr_s_t& op) {
      context.inc_pc();
      static constexpr uint32_t mask = (8 * sizeof(uint32_t) - 1);
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_i32();
      lhs >>= (rhs & mask);
   }
   [[gnu::always_inline]] inline void operator()(const i32_shr_u_t& op) {
      context.inc_pc();
      static constexpr uint32_t mask = (8 * sizeof(uint32_t) - 1);
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      lhs >>= (rhs & mask);
   }
   [[gnu::always_inline]] inline void operator()(const i32_rotl_t& op) {

      context.inc_pc();
      static constexpr uint32_t mask = (8 * sizeof(uint32_t) - 1);
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      uint32_t c = rhs;
      c &= mask;
      lhs = (lhs << c) | (lhs >> ((-c) & mask));
   }
   [[gnu::always_inline]] inline void operator()(const i32_rotr_t& op) {
      context.inc_pc();
      static constexpr uint32_t mask = (8 * sizeof(uint32_t) - 1);
      const auto& rhs = context.pop_operand().to_ui32();
      auto& lhs = context.peek_operand().to_ui32();
      uint32_t c = rhs;
      c &= mask;
      lhs = (lhs >> c) | (lhs << ((-c) & mask));
   }
   [[gnu::always_inline]] inline void operator()(const i64_clz_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_ui64();
      // __builtin_clzll(0) is undefined
      oper = oper == 0 ? 64 : __builtin_clzll(oper);
   }
   [[gnu::always_inline]] inline void operator()(const i64_ctz_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_ui64();
      // __builtin_clzll(0) is undefined
      oper = oper == 0 ? 64 : __builtin_ctzll(oper);
   }
   [[gnu::always_inline]] inline void operator()(const i64_popcnt_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_ui64();
      oper = __builtin_popcountll(oper);
   }
   [[gnu::always_inline]] inline void operator()(const i64_add_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      lhs += rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i64_sub_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      lhs -= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i64_mul_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      lhs *= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i64_div_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i64();
      auto& lhs = context.peek_operand().to_i64();
      detail::check<exceptions::interpreter>((rhs != 0), "i64.div_s divide by zero");
      detail::check<exceptions::interpreter>((!(lhs == std::numeric_limits<int64_t>::min() && rhs == -1)),
                                             "i64.div_s traps when I64_MAX/-1");
      lhs /= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i64_div_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      detail::check<exceptions::interpreter>((rhs != 0), "i64.div_u divide by zero");
      lhs /= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i64_rem_s_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_i64();
      auto& lhs = context.peek_operand().to_i64();
      detail::check<exceptions::interpreter>((rhs != 0), "i64.rem_s divide by zero");
      if (lhs == std::numeric_limits<int64_t>::min() && rhs == -1) [[unlikely]]
         lhs = 0;
      else
         lhs %= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i64_rem_u_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      detail::check<exceptions::interpreter>((rhs != 0), "i64.rem_s divide by zero");
      lhs %= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i64_and_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      lhs &= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i64_or_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      lhs |= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i64_xor_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      lhs ^= rhs;
   }
   [[gnu::always_inline]] inline void operator()(const i64_shl_t& op) {
      context.inc_pc();
      static constexpr uint64_t mask = (8 * sizeof(uint64_t) - 1);
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      lhs <<= (rhs & mask);
   }
   [[gnu::always_inline]] inline void operator()(const i64_shr_s_t& op) {
      context.inc_pc();
      static constexpr uint64_t mask = (8 * sizeof(uint64_t) - 1);
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_i64();
      lhs >>= (rhs & mask);
   }
   [[gnu::always_inline]] inline void operator()(const i64_shr_u_t& op) {
      context.inc_pc();
      static constexpr uint64_t mask = (8 * sizeof(uint64_t) - 1);
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      lhs >>= (rhs & mask);
   }
   [[gnu::always_inline]] inline void operator()(const i64_rotl_t& op) {
      context.inc_pc();
      static constexpr uint64_t mask = (8 * sizeof(uint64_t) - 1);
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      uint32_t c = rhs;
      c &= mask;
      lhs = (lhs << c) | (lhs >> (-c & mask));
   }
   [[gnu::always_inline]] inline void operator()(const i64_rotr_t& op) {
      context.inc_pc();
      static constexpr uint64_t mask = (8 * sizeof(uint64_t) - 1);
      const auto& rhs = context.pop_operand().to_ui64();
      auto& lhs = context.peek_operand().to_ui64();
      uint32_t c = rhs;
      c &= mask;
      lhs = (lhs >> c) | (lhs << (-c & mask));
   }
   [[gnu::always_inline]] inline void operator()(const f32_abs_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         oper = _wasm_f32_abs(oper);
      else
         oper = __builtin_fabsf(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f32_neg_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         oper = _wasm_f32_neg(oper);
      else
         oper = -oper;
   }
   [[gnu::always_inline]] inline void operator()(const f32_ceil_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         oper = _wasm_f32_ceil(oper);
      else
         oper = __builtin_ceilf(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f32_floor_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         oper = _wasm_f32_floor(oper);
      else
         oper = __builtin_floorf(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f32_trunc_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         oper = _wasm_f32_trunc(oper);
      else
         oper = __builtin_trunc(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f32_nearest_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         oper = _wasm_f32_nearest(oper);
      else
         oper = __builtin_nearbyintf(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f32_sqrt_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         oper = _wasm_f32_sqrt(oper);
      else
         oper = __builtin_sqrtf(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f32_add_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         lhs = _wasm_f32_add(lhs, rhs.to_f32());
      else
         lhs += rhs.to_f32();
   }
   [[gnu::always_inline]] inline void operator()(const f32_sub_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         lhs = _wasm_f32_sub(lhs, rhs.to_f32());
      else
         lhs -= rhs.to_f32();
   }
   [[gnu::always_inline]] inline void operator()(const f32_mul_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f32();
      if constexpr (use_softfloat) {
         lhs = _wasm_f32_mul(lhs, rhs.to_f32());
      } else
         lhs *= rhs.to_f32();
   }
   [[gnu::always_inline]] inline void operator()(const f32_div_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         lhs = _wasm_f32_div(lhs, rhs.to_f32());
      else
         lhs /= rhs.to_f32();
   }
   [[gnu::always_inline]] inline void operator()(const f32_min_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         lhs = _wasm_f32_min(lhs, rhs.to_f32());
      else
         lhs = __builtin_fminf(lhs, rhs.to_f32());
   }
   [[gnu::always_inline]] inline void operator()(const f32_max_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         lhs = _wasm_f32_max(lhs, rhs.to_f32());
      else
         lhs = __builtin_fmaxf(lhs, rhs.to_f32());
   }
   [[gnu::always_inline]] inline void operator()(const f32_copysign_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f32();
      if constexpr (use_softfloat)
         lhs = _wasm_f32_copysign(lhs, rhs.to_f32());
      else
         lhs = __builtin_copysignf(lhs, rhs.to_f32());
   }
   [[gnu::always_inline]] inline void operator()(const f64_abs_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         oper = _wasm_f64_abs(oper);
      else
         oper = __builtin_fabs(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f64_neg_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         oper = _wasm_f64_neg(oper);
      else
         oper = -oper;
   }
   [[gnu::always_inline]] inline void operator()(const f64_ceil_t& op) {

      context.inc_pc();
      auto& oper = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         oper = _wasm_f64_ceil(oper);
      else
         oper = __builtin_ceil(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f64_floor_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         oper = _wasm_f64_floor(oper);
      else
         oper = __builtin_floor(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f64_trunc_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         oper = _wasm_f64_trunc(oper);
      else
         oper = __builtin_trunc(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f64_nearest_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         oper = _wasm_f64_nearest(oper);
      else
         oper = __builtin_nearbyint(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f64_sqrt_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         oper = _wasm_f64_sqrt(oper);
      else
         oper = __builtin_sqrt(oper);
   }
   [[gnu::always_inline]] inline void operator()(const f64_add_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         lhs = _wasm_f64_add(lhs, rhs.to_f64());
      else
         lhs += rhs.to_f64();
   }
   [[gnu::always_inline]] inline void operator()(const f64_sub_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         lhs = _wasm_f64_sub(lhs, rhs.to_f64());
      else
         lhs -= rhs.to_f64();
   }
   [[gnu::always_inline]] inline void operator()(const f64_mul_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         lhs = _wasm_f64_mul(lhs, rhs.to_f64());
      else
         lhs *= rhs.to_f64();
   }
   [[gnu::always_inline]] inline void operator()(const f64_div_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         lhs = _wasm_f64_div(lhs, rhs.to_f64());
      else
         lhs /= rhs.to_f64();
   }
   [[gnu::always_inline]] inline void operator()(const f64_min_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         lhs = _wasm_f64_min(lhs, rhs.to_f64());
      else
         lhs = __builtin_fmin(lhs, rhs.to_f64());
   }
   [[gnu::always_inline]] inline void operator()(const f64_max_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         lhs = _wasm_f64_max(lhs, rhs.to_f64());
      else
         lhs = __builtin_fmax(lhs, rhs.to_f64());
   }
   [[gnu::always_inline]] inline void operator()(const f64_copysign_t& op) {
      context.inc_pc();
      const auto& rhs = context.pop_operand();
      auto& lhs = context.peek_operand().to_f64();
      if constexpr (use_softfloat)
         lhs = _wasm_f64_copysign(lhs, rhs.to_f64());
      else
         lhs = __builtin_copysign(lhs, rhs.to_f64());
   }
   [[gnu::always_inline]] inline void operator()(const i32_wrap_i64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      oper = i32_const_t{static_cast<int32_t>(oper.to_i64())};
   }
   [[gnu::always_inline]] inline void operator()(const i32_trunc_s_f32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = i32_const_t{_wasm_f32_trunc_i32s(oper.to_f32())};
      } else {
         float af = oper.to_f32();
         detail::check<exceptions::interpreter>((!((af >= 2147483648.0f) || (af < -2147483648.0f))),
                                                "Error, f32.trunc_s/i32 overflow");
         detail::check<exceptions::interpreter>((!__builtin_isnan(af)), "Error, f32.trunc_s/i32 unrepresentable");
         oper = i32_const_t{static_cast<int32_t>(af)};
      }
   }
   [[gnu::always_inline]] inline void operator()(const i32_trunc_u_f32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = i32_const_t{_wasm_f32_trunc_i32u(oper.to_f32())};
      } else {
         float af = oper.to_f32();
         detail::check<exceptions::interpreter>((!((af >= 4294967296.0f) || (af <= -1.0f))),
                                                "Error, f32.trunc_u/i32 overflow");
         detail::check<exceptions::interpreter>((!__builtin_isnan(af)), "Error, f32.trunc_u/i32 unrepresentable");
         oper = i32_const_t{static_cast<uint32_t>(af)};
      }
   }
   [[gnu::always_inline]] inline void operator()(const i32_trunc_s_f64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = i32_const_t{_wasm_f64_trunc_i32s(oper.to_f64())};
      } else {
         double af = oper.to_f64();
         detail::check<exceptions::interpreter>((!((af >= 2147483648.0) || (af < -2147483648.0))),
                                                "Error, f64.trunc_s/i32 overflow");
         detail::check<exceptions::interpreter>((!__builtin_isnan(af)), "Error, f64.trunc_s/i32 unrepresentable");
         oper = i32_const_t{static_cast<int32_t>(af)};
      }
   }
   [[gnu::always_inline]] inline void operator()(const i32_trunc_u_f64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = i32_const_t{_wasm_f64_trunc_i32u(oper.to_f64())};
      } else {
         double af = oper.to_f64();
         detail::check<exceptions::interpreter>((!((af >= 4294967296.0) || (af <= -1.0))),
                                                "Error, f64.trunc_u/i32 overflow");
         detail::check<exceptions::interpreter>((!__builtin_isnan(af)), "Error, f64.trunc_u/i32 unrepresentable");
         oper = i32_const_t{static_cast<uint32_t>(af)};
      }
   }
   [[gnu::always_inline]] inline void operator()(const i64_extend_s_i32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      oper = i64_const_t{static_cast<int64_t>(oper.to_i32())};
   }
   [[gnu::always_inline]] inline void operator()(const i64_extend_u_i32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      oper = i64_const_t{static_cast<uint64_t>(oper.to_ui32())};
   }
   [[gnu::always_inline]] inline void operator()(const i64_trunc_s_f32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = i64_const_t{_wasm_f32_trunc_i64s(oper.to_f32())};
      } else {
         float af = oper.to_f32();
         detail::check<exceptions::interpreter>((!((af >= 9223372036854775808.0f) || (af < -9223372036854775808.0f))),
                                                "Error, f32.trunc_s/i64 overflow");
         detail::check<exceptions::interpreter>((!__builtin_isnan(af)), "Error, f32.trunc_s/i64 unrepresentable");
         oper = i64_const_t{static_cast<int64_t>(af)};
      }
   }
   [[gnu::always_inline]] inline void operator()(const i64_trunc_u_f32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = i64_const_t{_wasm_f32_trunc_i64u(oper.to_f32())};
      } else {
         float af = oper.to_f32();
         detail::check<exceptions::interpreter>((!((af >= 18446744073709551616.0f) || (af <= -1.0f))),
                                                "Error, f32.trunc_u/i64 overflow");
         detail::check<exceptions::interpreter>((!__builtin_isnan(af)), "Error, f32.trunc_u/i64 unrepresentable");
         oper = i64_const_t{static_cast<uint64_t>(af)};
      }
   }
   [[gnu::always_inline]] inline void operator()(const i64_trunc_s_f64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = i64_const_t{_wasm_f64_trunc_i64s(oper.to_f64())};
      } else {
         double af = oper.to_f64();
         detail::check<exceptions::interpreter>((!((af >= 9223372036854775808.0) || (af < -9223372036854775808.0))),
                                                "Error, f64.trunc_s/i64 overflow");
         detail::check<exceptions::interpreter>((!__builtin_isnan(af)), "Error, f64.trunc_s/i64 unrepresentable");
         oper = i64_const_t{static_cast<int64_t>(af)};
      }
   }
   [[gnu::always_inline]] inline void operator()(const i64_trunc_u_f64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = i64_const_t{_wasm_f64_trunc_i64u(oper.to_f64())};
      } else {
         double af = oper.to_f64();
         detail::check<exceptions::interpreter>((!((af >= 18446744073709551616.0) || (af <= -1.0))),
                                                "Error, f64.trunc_u/i64 overflow");
         detail::check<exceptions::interpreter>((!__builtin_isnan(af)), "Error, f64.trunc_u/i64 unrepresentable");
         oper = i64_const_t{static_cast<uint64_t>(af)};
      }
   }
   [[gnu::always_inline]] inline void operator()(const f32_convert_s_i32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = f32_const_t{_wasm_i32_to_f32(oper.to_i32())};
      } else {
         oper = f32_const_t{static_cast<float>(oper.to_i32())};
      }
   }
   [[gnu::always_inline]] inline void operator()(const f32_convert_u_i32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = f32_const_t{_wasm_ui32_to_f32(oper.to_ui32())};
      } else {
         oper = f32_const_t{static_cast<float>(oper.to_ui32())};
      }
   }
   [[gnu::always_inline]] inline void operator()(const f32_convert_s_i64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = f32_const_t{_wasm_i64_to_f32(oper.to_i64())};
      } else {
         oper = f32_const_t{static_cast<float>(oper.to_i64())};
      }
   }
   [[gnu::always_inline]] inline void operator()(const f32_convert_u_i64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = f32_const_t{_wasm_ui64_to_f32(oper.to_ui64())};
      } else {
         oper = f32_const_t{static_cast<float>(oper.to_ui64())};
      }
   }
   [[gnu::always_inline]] inline void operator()(const f32_demote_f64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = f32_const_t{_wasm_f64_demote(oper.to_f64())};
      } else {
         oper = f32_const_t{static_cast<float>(oper.to_f64())};
      }
   }
   [[gnu::always_inline]] inline void operator()(const f64_convert_s_i32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = f64_const_t{_wasm_i32_to_f64(oper.to_i32())};
      } else {
         oper = f64_const_t{static_cast<double>(oper.to_i32())};
      }
   }
   [[gnu::always_inline]] inline void operator()(const f64_convert_u_i32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = f64_const_t{_wasm_ui32_to_f64(oper.to_ui32())};
      } else {
         oper = f64_const_t{static_cast<double>(oper.to_ui32())};
      }
   }
   [[gnu::always_inline]] inline void operator()(const f64_convert_s_i64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = f64_const_t{_wasm_i64_to_f64(oper.to_i64())};
      } else {
         oper = f64_const_t{static_cast<double>(oper.to_i64())};
      }
   }
   [[gnu::always_inline]] inline void operator()(const f64_convert_u_i64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = f64_const_t{_wasm_ui64_to_f64(oper.to_ui64())};
      } else {
         oper = f64_const_t{static_cast<double>(oper.to_ui64())};
      }
   }
   [[gnu::always_inline]] inline void operator()(const f64_promote_f32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      if constexpr (use_softfloat) {
         oper = f64_const_t{_wasm_f32_promote(oper.to_f32())};
      } else {
         oper = f64_const_t{static_cast<double>(oper.to_f32())};
      }
   }
   [[gnu::always_inline]] inline void operator()(const i32_reinterpret_f32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      oper = i32_const_t{oper.to_fui32()};
   }
   [[gnu::always_inline]] inline void operator()(const i64_reinterpret_f64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      oper = i64_const_t{oper.to_fui64()};
   }
   [[gnu::always_inline]] inline void operator()(const f32_reinterpret_i32_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      oper = f32_const_t{oper.to_ui32()};
   }
   [[gnu::always_inline]] inline void operator()(const f64_reinterpret_i64_t& op) {
      context.inc_pc();
      auto& oper = context.peek_operand();
      oper = f64_const_t{oper.to_ui64()};
   }
};

} // namespace forge::vm::wasm

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

namespace forge::vm::wasm {

// Fixes a duplicate symbol build issue when building with `-fvisibility=hidden`
__attribute__((visibility("default"))) extern thread_local std::atomic<sigjmp_buf*> signal_dest;

__attribute__((visibility("default"))) extern thread_local std::span<std::byte> code_memory_range;

__attribute__((visibility("default"))) extern thread_local std::span<std::byte> memory_range;

__attribute__((visibility("default"))) extern thread_local std::atomic<bool> timed_run_has_timed_out;

// Fixes a duplicate symbol build issue when building with `-fvisibility=hidden`
__attribute__((visibility("default"))) extern thread_local std::exception_ptr saved_exception;

template <int Sig> inline struct sigaction prev_signal_handler;

void signal_handler(int signal, siginfo_t* info, void* context);

// only valid inside invoke_with_signal_handler.
// This is a workaround for the fact that it
// is currently unsafe to throw an exception through
// a jit frame.
template <typename F> inline void longjmp_on_exception(F&& f) {
   static_assert(std::is_trivially_destructible_v<std::decay_t<F>>,
                 "longjmp has undefined behavior when it bypasses destructors.");
   bool caught_exception = false;
   try {
      f();
   } catch (...) {
      saved_exception = std::current_exception();
      // Cannot safely longjmp from inside the catch,
      // as that will leak the exception.
      caught_exception = true;
   }
   if (caught_exception) {
      sigset_t block_mask;
      sigemptyset(&block_mask);
      sigaddset(&block_mask, SIGPROF);
      pthread_sigmask(SIG_BLOCK, &block_mask, nullptr);
      sigjmp_buf* dest = std::atomic_load(&signal_dest);
      siglongjmp(*dest, -1);
   }
}

template <typename E> [[noreturn]] inline void throw_(const char* msg) {
   saved_exception = std::make_exception_ptr(E{msg});
   sigset_t block_mask;
   sigemptyset(&block_mask);
   sigaddset(&block_mask, SIGPROF);
   pthread_sigmask(SIG_BLOCK, &block_mask, nullptr);
   sigjmp_buf* dest = std::atomic_load(&signal_dest);
   siglongjmp(*dest, -1);
}

void setup_signal_handler_impl();
void setup_signal_handler();

/// Call a function with a signal handler installed.  If this thread is
/// signalled during the execution of f, the function e will be called with
/// the signal number as an argument.  If f creates any automatic variables
/// with non-trivial destructors, then it must mask the relevant signals
/// during the lifetime of these objects or the behavior is undefined.
///
/// signals handled: SIGSEGV, SIGBUS (except on Linux), SIGFPE
///
// Make this noinline to prevent possible corruption of the caller's local variables.
// It's unlikely, but I'm not sure that it can definitely be ruled out if both
// this and f are inlined and f modifies locals from the caller.
template <typename F, typename E>
[[gnu::noinline]] auto invoke_with_signal_handler(F&& f, E&& e, growable_allocator* code_allocator,
                                                  wasm_allocator* mem_allocator) {
   setup_signal_handler();
   sigjmp_buf dest;
   sigjmp_buf* volatile old_signal_handler = nullptr;
   code_memory_range = code_allocator ? code_allocator->get_code_span() : std::span<std::byte>{};
   memory_range = mem_allocator ? mem_allocator->get_span() : std::span<std::byte>{};
   int sig;
   if ((sig = sigsetjmp(dest, 1)) == 0) {
      // Note: Cannot use RAII, as non-trivial destructors w/ longjmp
      // have undefined behavior. [csetjmp.syn]
      //
      // Warning: The order of operations is critical here.
      // We also have to register signal_dest before unblocking
      // signals to make sure that only our signal handler is executed
      // if the caller has previously blocked signals.
      old_signal_handler = std::atomic_exchange(&signal_dest, &dest);
      sigset_t unblock_mask, old_sigmask; // Might not be preserved across longjmp
      sigemptyset(&unblock_mask);
      sigaddset(&unblock_mask, SIGSEGV);
      sigaddset(&unblock_mask, SIGBUS);
      sigaddset(&unblock_mask, SIGFPE);
      sigaddset(&unblock_mask, SIGPROF);
      pthread_sigmask(SIG_UNBLOCK, &unblock_mask, &old_sigmask);
      try {
         f();
         pthread_sigmask(SIG_SETMASK, &old_sigmask, nullptr);
         std::atomic_store(&signal_dest, old_signal_handler);
      } catch (...) {
         pthread_sigmask(SIG_SETMASK, &old_sigmask, nullptr);
         std::atomic_store(&signal_dest, old_signal_handler);
         throw;
      }
   } else {
      std::atomic_store(&signal_dest, old_signal_handler);
      if (sig == -1) {
         std::exception_ptr exception = std::move(saved_exception);
         saved_exception = nullptr;
         std::rethrow_exception(exception);
      } else {
         e(sig);
      }
   }
}

template <typename F, typename E>
auto invoke_with_signal_handler(F&& f, E&& e, growable_allocator& code_allocator, wasm_allocator* mem_allocator) {
   return invoke_with_signal_handler(std::forward<F>(f), std::forward<E>(e), &code_allocator, mem_allocator);
}

} // namespace forge::vm::wasm

// OSX requires _XOPEN_SOURCE to #include <ucontext.h>
#ifdef __APPLE__
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#endif

namespace forge::vm::wasm {

struct null_host_functions {
   template <typename... A> void operator()(A&&...) const {
      detail::fail<exceptions::interpreter>("Should never get here because it's impossible to link a module "
                                            "that imports any host functions, when no host functions are available");
   }
};

namespace detail {
template <typename HostFunctions> struct host_type {
   using type = typename HostFunctions::host_type_t;
};
template <> struct host_type<std::nullptr_t> {
   using type = std::nullptr_t;
};

template <typename HF> using host_type_t = typename host_type<HF>::type;

template <typename HostFunctions> struct type_converter {
   using type = typename HostFunctions::type_converter_t;
};
template <> struct type_converter<std::nullptr_t> {
   using type = forge::vm::wasm::type_converter<std::nullptr_t, forge::vm::wasm::execution_interface>;
};

template <typename HF> using type_converter_t = typename type_converter<HF>::type;

template <typename HostFunctions> struct host_invoker {
   using type = HostFunctions;
};
template <> struct host_invoker<std::nullptr_t> {
   using type = null_host_functions;
};
template <typename HF> using host_invoker_t = typename host_invoker<HF>::type;
} // namespace detail

template <typename Derived, typename Host, bool IsJit> class execution_context_base {
   using host_type = detail::host_type_t<Host>;

 public:
   Derived& derived() {
      return static_cast<Derived&>(*this);
   }
   execution_context_base() {}
   execution_context_base(module* m) : _mod(m) {}

   inline void initialize_globals() {
      if constexpr (IsJit) {
         return initialize_globals_impl(*_mod->jit_mod);
      } else {
         return initialize_globals_impl(*_mod);
      }
   }

   template <typename Module> inline void initialize_globals_impl(const Module& mod) {
      detail::check<exceptions::memory>((_globals.empty()), "initialize_globals called on non-empty _globals");
      _globals.reserve(mod.globals.size());
      for (uint32_t i = 0; i < mod.globals.size(); i++) {
         _globals.emplace_back(mod.globals[i].init);
      }
   }

   inline int32_t grow_linear_memory(int32_t pages) {
      if constexpr (IsJit) {
         return grow_linear_memory_impl(*_mod->jit_mod, pages);
      } else {
         return grow_linear_memory_impl(*_mod, pages);
      }
   }

   template <typename Module> inline int32_t grow_linear_memory_impl(const Module& mod, int32_t pages) {
      const int32_t sz = _wasm_alloc->get_current_page();
      if (pages < 0) {
         if (sz + pages < 0)
            return -1;
         _wasm_alloc->free<char>(-pages);
      } else {
         if (!mod.memories.size() || _max_pages - sz < static_cast<uint32_t>(pages) ||
             (mod.memories[0].limits.flags && (static_cast<int32_t>(mod.memories[0].limits.maximum) - sz < pages)))
            return -1;
         _wasm_alloc->alloc<char>(pages);
      }
      return sz;
   }

   inline int32_t current_linear_memory() const {
      return _wasm_alloc->get_current_page();
   }
   inline void exit(std::error_code err = std::error_code()) {
      // FIXME: system_error?
      _error_code = err;
      throw exceptions::exit{"Exiting"};
   }

   inline void set_module(module* mod) {
      _mod = mod;
   }
   inline module& get_module() {
      return *_mod;
   }
   inline void set_wasm_allocator(wasm_allocator* alloc) {
      _wasm_alloc = alloc;
   }
   inline auto get_wasm_allocator() {
      return _wasm_alloc;
   }
   inline char* linear_memory() {
      return _linear_memory;
   }
   inline auto& get_operand_stack() {
      return _os;
   }
   inline const auto& get_operand_stack() const {
      return _os;
   }
   inline auto get_interface() {
      return execution_interface{_linear_memory, &_os};
   }
   void set_max_pages(std::uint32_t max_pages) {
      _max_pages = std::min(max_pages, static_cast<std::uint32_t>(::forge::vm::wasm::max_pages));
   }

   inline std::error_code get_error_code() const {
      return _error_code;
   }

   template <typename Module> inline void reset(Module& mod) {
      detail::check<exceptions::interpreter>((_mod->error == nullptr), _mod->error);

      // Reset the capacity of underlying memory used by operand stack if it is
      // greater than initial_stack_size
      _os.reset_capacity();

      _linear_memory = _wasm_alloc->get_base_ptr<char>();
      if (mod.memories.size()) {
         detail::check<exceptions::allocation>((mod.memories[0].limits.initial <= _max_pages),
                                               "Cannot allocate initial linear memory.");
         _wasm_alloc->reset(mod.memories[0].limits.initial);
      } else
         _wasm_alloc->reset();

      for (uint32_t i = 0; i < mod.data.size(); i++) {
         const auto& data_seg = mod.data[i];
         uint32_t offset = data_seg.offset.value.i32; // force to unsigned
         auto available_memory = mod.memories[0].limits.initial * static_cast<uint64_t>(page_size);
         auto required_memory = static_cast<uint64_t>(offset) + data_seg.data.size();
         detail::check<exceptions::memory>((required_memory <= available_memory), "data out of range");
         auto addr = _linear_memory + offset;
         if (data_seg.data.size())
            memcpy((char*)(addr), data_seg.data.data(), data_seg.data.size());
      }

      // Globals can be different from one WASM code to another.
      // Need to clear _globals at the start of an execution.
      _globals.clear();
      _globals.reserve(mod.globals.size());
      for (uint32_t i = 0; i < mod.globals.size(); i++) {
         _globals.emplace_back(mod.globals[i].init);
      }
   }

   template <typename Visitor, typename... Args>
   inline std::optional<operand_stack_elem> execute(host_type* host, Visitor&& visitor, const std::string_view func,
                                                    Args&&... args) {
      uint32_t func_index = _mod->get_exported_function(func);
      return derived().execute(host, std::forward<Visitor>(visitor), func_index, std::forward<Args>(args)...);
   }

   template <typename Visitor, typename... Args> inline void execute_start(host_type* host, Visitor&& visitor) {
      if (_mod->start != std::numeric_limits<uint32_t>::max())
         derived().execute(host, std::forward<Visitor>(visitor), _mod->start);
   }

 protected:
   template <typename Func_type, typename... Args> static void type_check_args(const Func_type& ft, Args&&...) {
      detail::check<exceptions::interpreter>((sizeof...(Args) == ft.param_types.size()), "wrong number of arguments");
      uint32_t i = 0;
      detail::check<exceptions::interpreter>(
          ((... && (to_wasm_type_v<detail::type_converter_t<Host>, Args> == ft.param_types.at(i++)))),
          "unexpected argument type");
   }

   static void handle_signal(int sig) {
      switch (sig) {
      case SIGSEGV:
      case SIGBUS:
      case SIGFPE:
         break;
      default:
         /* TODO fix this */
         assert(!"??????");
      }
      throw exceptions::memory{"wasm memory out-of-bounds"};
   }

   char* _linear_memory = nullptr;
   module* _mod = nullptr;
   wasm_allocator* _wasm_alloc;
   uint32_t _max_pages = max_pages;
   detail::host_invoker_t<Host> _rhf;
   std::error_code _error_code;
   operand_stack _os;
   std::vector<init_expr> _globals;
};

struct jit_visitor {
   template <typename T> jit_visitor(T&&) {}
};

template <typename Host>
class null_execution_context : public execution_context_base<null_execution_context<Host>, Host, false> {
   using base_type = execution_context_base<null_execution_context<Host>, Host, false>;

 public:
   null_execution_context() {}
   null_execution_context(module& m, std::uint32_t max_call_depth) : base_type(&m) {}
};

template <bool EnableBacktrace> struct frame_info_holder {};
template <> struct frame_info_holder<true> {
   void* volatile _bottom_frame = nullptr;
   void* volatile _top_frame = nullptr;
};

template <typename Host, bool EnableBacktrace = false>
class jit_execution_context : public frame_info_holder<EnableBacktrace>,
                              public execution_context_base<jit_execution_context<Host, EnableBacktrace>, Host, true> {
   using base_type = execution_context_base<jit_execution_context<Host, EnableBacktrace>, Host, true>;
   using host_type = detail::host_type_t<Host>;

 public:
   using base_type::_error_code;
   using base_type::_globals;
   using base_type::_mod;
   using base_type::_rhf;
   using base_type::base_type;
   using base_type::execute;
   using base_type::get_interface;
   using base_type::get_operand_stack;
   using base_type::handle_signal;
   using base_type::linear_memory;

   jit_execution_context() {}

   jit_execution_context(module& m, std::uint32_t max_call_depth)
       : base_type(&m), _remaining_call_depth(max_call_depth) {}

   void set_max_call_depth(std::uint32_t max_call_depth) {
      _remaining_call_depth = max_call_depth;
   }

   inline native_value call_host_function(native_value* stack, uint32_t index) {
      const auto& ft = _mod->jit_mod->get_function_type(index);
      uint32_t num_params = ft.param_types.size();
#ifndef NDEBUG
      uint32_t original_operands = get_operand_stack().size();
#endif
      for (uint32_t i = 0; i < ft.param_types.size(); ++i) {
         switch (ft.param_types[i]) {
         case i32:
            get_operand_stack().push(i32_const_t{stack[num_params - i - 1].i32});
            break;
         case i64:
            get_operand_stack().push(i64_const_t{stack[num_params - i - 1].i64});
            break;
         case f32:
            get_operand_stack().push(f32_const_t{stack[num_params - i - 1].f32});
            break;
         case f64:
            get_operand_stack().push(f64_const_t{stack[num_params - i - 1].f64});
            break;
         default:
            assert(!"Unexpected type in param_types.");
         }
      }
      _rhf(_host, get_interface(), _mod->jit_mod->import_functions[index]);
      native_value result{uint64_t{0}};
      // guarantee that the junk bits are zero, to avoid problems.
      auto set_result = [&result](auto val) { std::memcpy(&result, &val, sizeof(val)); };
      if (ft.return_count) {
         operand_stack_elem el = get_operand_stack().pop();
         switch (ft.return_type) {
         case i32:
            set_result(el.to_ui32());
            break;
         case i64:
            set_result(el.to_ui64());
            break;
         case f32:
            set_result(el.to_f32());
            break;
         case f64:
            set_result(el.to_f64());
            break;
         default:
            assert(!"Unexpected function return type.");
         }
      }

      assert(get_operand_stack().size() == original_operands);
      return result;
   }

   inline void reset() {
      base_type::reset(*(_mod->jit_mod));
      get_operand_stack().eat(0);
   }

   template <typename... Args>
   inline std::optional<operand_stack_elem> execute(host_type* host, jit_visitor, uint32_t func_index, Args&&... args) {
      auto saved_host = _host;
      auto saved_os_size = get_operand_stack().size();
      auto g = scope_guard([&]() {
         _host = saved_host;
         get_operand_stack().eat(saved_os_size);
      });

      _host = host;

      const auto& ft = _mod->jit_mod->get_function_type(func_index);
      this->type_check_args(ft, std::forward<Args>(args)...); // args not modified by type_check_args
      native_value result;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
      // Calling execute() with no `args` (i.e. `execute(host_type,jit_visitor,uint32_t)`) results in a "statement has
      // no effect [-Werror=unused-value]" warning on this line. Dissable warning.
      native_value args_raw[] = {transform_arg(std::forward<Args>(args))...};
#pragma GCC diagnostic pop

      try {
         if (func_index < _mod->jit_mod->get_imported_functions_size()) {
            std::reverse(args_raw + 0, args_raw + sizeof...(Args));
            result = call_host_function(args_raw, func_index);
         } else {
            std::size_t maximum_stack_usage =
                (_mod->maximum_stack + 2 /*frame ptr + return ptr*/) * (_remaining_call_depth + 1) + sizeof...(Args) +
                4 /* scratch space */;
            stack_allocator alt_stack(maximum_stack_usage * sizeof(native_value));
            // reserve 24 bytes for data accessed by inline assembly
            void* stack = alt_stack.top();
            if (stack) {
               stack = static_cast<char*>(stack) - 24;
            }
            auto fn = reinterpret_cast<native_value (*)(void*, void*)>(
                _mod->jit_mod->jit_code_offset[func_index - _mod->jit_mod->get_imported_functions_size()] +
                _mod->allocator._code_base);

            if constexpr (EnableBacktrace) {
               sigset_t block_mask;
               sigemptyset(&block_mask);
               sigaddset(&block_mask, SIGPROF);
               pthread_sigmask(SIG_BLOCK, &block_mask, nullptr);
               auto restore = scope_guard{[this, &block_mask] {
                  this->_top_frame = nullptr;
                  this->_bottom_frame = nullptr;
                  pthread_sigmask(SIG_UNBLOCK, &block_mask, nullptr);
               }};

               ::forge::vm::wasm::invoke_with_signal_handler(
                   [&]() { result = execute<sizeof...(Args)>(args_raw, fn, this, base_type::linear_memory(), stack); },
                   &handle_signal, _mod->allocator, base_type::get_wasm_allocator());
            } else {
               ::forge::vm::wasm::invoke_with_signal_handler(
                   [&]() { result = execute<sizeof...(Args)>(args_raw, fn, this, base_type::linear_memory(), stack); },
                   &handle_signal, _mod->allocator, base_type::get_wasm_allocator());
            }
         }
      } catch (exceptions::exit&) {
         return {};
      }

      if (!ft.return_count)
         return {};
      else
         switch (ft.return_type) {
         case i32:
            return {i32_const_t{result.i32}};
         case i64:
            return {i64_const_t{result.i64}};
         case f32:
            return {f32_const_t{result.f32}};
         case f64:
            return {f64_const_t{result.f64}};
         default:
            assert(!"Unexpected function return type");
         }
      __builtin_unreachable();
   }

#ifdef __x86_64__
   int backtrace(void** out, int count, void* uc) const {
      static_assert(EnableBacktrace);
      void* end = this->_top_frame;
      if (end == nullptr)
         return 0;
      void* rbp;
      int i = 0;
      if (this->_bottom_frame) {
         rbp = this->_bottom_frame;
      } else if (count != 0) {
         if (uc) {
#ifdef __APPLE__
            auto rip = reinterpret_cast<unsigned char*>(static_cast<ucontext_t*>(uc)->uc_mcontext->__ss.__rip);
            rbp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext->__ss.__rbp);
            auto rsp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext->__ss.__rsp);
#elif defined __FreeBSD__
            auto rip = reinterpret_cast<unsigned char*>(static_cast<ucontext_t*>(uc)->uc_mcontext.mc_rip);
            rbp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext.mc_rbp);
            auto rsp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext.mc_rsp);
#else
            auto rip = reinterpret_cast<unsigned char*>(static_cast<ucontext_t*>(uc)->uc_mcontext.gregs[REG_RIP]);
            rbp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext.gregs[REG_RBP]);
            auto rsp = reinterpret_cast<void*>(static_cast<ucontext_t*>(uc)->uc_mcontext.gregs[REG_RSP]);
#endif
            out[i++] = rip;
            // If we were interrupted in the function prologue or epilogue,
            // avoid dropping the parent frame.
            auto code_base = reinterpret_cast<const unsigned char*>(_mod->allocator.get_code_start());
            auto code_end = code_base + _mod->allocator._code_size;
            if (rip >= code_base && rip < code_end && count > 1) {
               // function prologue
               if (*reinterpret_cast<const unsigned char*>(rip) == 0x55) {
                  if (rip != *static_cast<void**>(rsp)) { // Ignore fake frame set up for softfloat calls
                     out[i++] = *static_cast<void**>(rsp);
                  }
               } else if (rip[0] == 0x48 && rip[1] == 0x89 && (rip[2] == 0xe5 || rip[2] == 0x27)) {
                  if ((rip - 1) != static_cast<void**>(rsp)[1]) { // Ignore fake frame set up for softfloat calls
                     out[i++] = static_cast<void**>(rsp)[1];
                  }
               }
               // function epilogue
               else if (rip[0] == 0xc3) {
                  out[i++] = *static_cast<void**>(rsp);
               }
            }
         } else {
            rbp = __builtin_frame_address(0);
         }
      }
      while (i < count) {
         void* rip = static_cast<void**>(rbp)[1];
         if (rbp == end)
            break;
         out[i++] = rip;
         rbp = *static_cast<void**>(rbp);
      }
      return i;
   }

   static constexpr bool async_backtrace() {
      return EnableBacktrace;
   }
#endif

   inline int32_t get_global_i32(uint32_t index) {
      return _globals[index].value.i32;
   }

   inline int64_t get_global_i64(uint32_t index) {
      return _globals[index].value.i64;
   }

   inline uint32_t get_global_f32(uint32_t index) {
      return _globals[index].value.f32;
   }

   inline uint64_t get_global_f64(uint32_t index) {
      return _globals[index].value.f64;
   }

   inline void set_global_i32(uint32_t index, int32_t value) {
      _globals[index].value.i32 = value;
   }

   inline void set_global_i64(uint32_t index, int64_t value) {
      _globals[index].value.i64 = value;
   }

   inline void set_global_f32(uint32_t index, uint32_t value) {
      _globals[index].value.f32 = value;
   }

   inline void set_global_f64(uint32_t index, uint64_t value) {
      _globals[index].value.f64 = value;
   }

 protected:
   template <typename T> native_value transform_arg(T&& value) {
      // make sure that the garbage bits are always zero.
      native_value result;
      std::memset(&result, 0, sizeof(result));
      auto tc = detail::type_converter_t<Host>{_host, get_interface()};
      auto transformed_value = detail::resolve_result(tc, std::forward<T>(value)).data;
      std::memcpy(&result, &transformed_value, sizeof(transformed_value));
      return result;
   }

#ifdef __x86_64__
   /* TODO abstract this and clean this up a bit, this really doesn't belong here */
   template <int Count>
   static native_value execute(native_value* data, native_value (*fun)(void*, void*), jit_execution_context* context,
                               void* linear_memory, void* stack) {
      static_assert(sizeof(native_value) == 8, "8-bytes expected for native_value");
      native_value result;
      unsigned stack_check = context->_remaining_call_depth;
      // TODO refactor this whole thing to not need all of this, should be generated from the backend
      // currently ignoring register c++17 warning
      register void* stack_top asm("r12") = stack;
      // 0x1f80 is the default MXCSR value
#define ASM_CODE(before, after)                                                                                        \
   asm volatile(                                                                                                       \
       "test %[stack_top], %[stack_top]; "                                                                             \
       "jnz 3f; "                                                                                                      \
       "mov %%rsp, %[stack_top]; "                                                                                     \
       "sub $0x98, %%rsp; " /* red-zone + 24 bytes*/                                                                   \
       "mov %[stack_top], (%%rsp); "                                                                                   \
       "jmp 4f; "                                                                                                      \
       "3: "                                                                                                           \
       "mov %%rsp, (%[stack_top]); "                                                                                   \
       "mov %[stack_top], %%rsp; "                                                                                     \
       "4: "                                                                                                           \
       "stmxcsr 16(%%rsp); "                                                                                           \
       "mov $0x1f80, %%rax; "                                                                                          \
       "mov %%rax, 8(%%rsp); "                                                                                         \
       "ldmxcsr 8(%%rsp); "                                                                                            \
       "mov %[Count], %%rax; "                                                                                         \
       "test %%rax, %%rax; "                                                                                           \
       "jz 2f; "                                                                                                       \
       "1: "                                                                                                           \
       "movq (%[data]), %%r8; "                                                                                        \
       "lea 8(%[data]), %[data]; "                                                                                     \
       "pushq %%r8; "                                                                                                  \
       "dec %%rax; "                                                                                                   \
       "jnz 1b; "                                                                                                      \
       "2: " before "callq *%[fun]; " after "add %[StackOffset], %%rsp; "                                              \
       "ldmxcsr 16(%%rsp); "                                                                                           \
       "mov (%%rsp), %%rsp; "    /* Force explicit register allocation, because otherwise it's too hard to get the     \
                                    clobbers right. */                                                                 \
       : [result] "=&a"(result), /* output, reused as a scratch register */                                            \
         [data] "+d"(data), [fun] "+c"(fun), [stack_top] "+r"(stack_top) /* input only, but may be clobbered */        \
       : [context] "D"(context), [linear_memory] "S"(linear_memory), [StackOffset] "n"(Count * 8), [Count] "n"(Count), \
         "b"(stack_check)              /* input */                                                                     \
       : "memory", "cc", /* clobber */ /* call clobbered registers, that are not otherwise used */                     \
         /*"rax", "rcx", "rdx", "rsi", "rdi",*/ "r8", "r9", "r10", "r11", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4",      \
         "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "mm0", "mm1",   \
         "mm2", "mm3", "mm4", "mm5", "mm6", "mm6", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)",         \
         "st(7)");
      if constexpr (!EnableBacktrace) {
         ASM_CODE("", "");
      } else {
         ASM_CODE("movq %%rbp, 8(%[context]); ", "xor %[fun], %[fun]; "
                                                 "mov %[fun], 8(%[context]); ");
      }
#undef ASM_CODE
      return result;
   }
#endif

   host_type* _host = nullptr;
   uint32_t _remaining_call_depth = 0;
};

template <typename Host> class execution_context : public execution_context_base<execution_context<Host>, Host, false> {
   using base_type = execution_context_base<execution_context<Host>, Host, false>;
   using host_type = detail::host_type_t<Host>;

 public:
   using base_type::_error_code;
   using base_type::_globals;
   using base_type::_mod;
   using base_type::_rhf;
   using base_type::get_interface;
   using base_type::get_operand_stack;
   using base_type::handle_signal;
   using base_type::linear_memory;

   execution_context() : base_type(), _halt(exit_t{}) {}

   execution_context(module& m, uint32_t max_call_depth)
       : base_type(&m), _base_allocator{max_call_depth * sizeof(activation_frame)},
         _as{max_call_depth, _base_allocator}, _halt(exit_t{}) {}

   void set_max_call_depth(uint32_t max_call_depth) {
      static_assert(std::is_trivially_move_assignable_v<call_stack>,
                    "This is seriously broken if call_stack move assignment might use the existing memory");
      std::size_t mem_size = max_call_depth * sizeof(activation_frame);
      if (mem_size > _base_allocator.mem_size) {
         _base_allocator = bounded_allocator{mem_size};
         _as = call_stack{max_call_depth, _base_allocator};
      } else if (max_call_depth != _as.capacity()) {
         _base_allocator.index = 0;
         _as = call_stack{max_call_depth, _base_allocator};
      }
   }

   inline void call(uint32_t index) {
      // TODO validate index is valid
      if (index < _mod->get_imported_functions_size()) {
         // TODO validate only importing functions
         const auto& ft = _mod->types[_mod->imports[index].type.func_t];
         type_check(ft);
         inc_pc();
         push_call(activation_frame{nullptr, 0});
         _rhf(_state.host, get_interface(), _mod->import_functions[index]);
         pop_call();
      } else {
         // const auto& ft = _mod->types[_mod->functions[index - _mod->get_imported_functions_size()]];
         // type_check(ft);
         push_call(index);
         setup_locals(index);
         set_pc(_mod->get_function_pc(index));
      }
   }

   void print_stack() {
      std::cout << "STACK { ";
      for (int i = 0; i < get_operand_stack().size(); i++) {
         std::cout << "(" << i << ")";
         visit(overloaded{[&](i32_const_t el) { std::cout << "i32:" << el.data.ui << ", "; },
                          [&](i64_const_t el) { std::cout << "i64:" << el.data.ui << ", "; },
                          [&](f32_const_t el) { std::cout << "f32:" << el.data.f << ", "; },
                          [&](f64_const_t el) { std::cout << "f64:" << el.data.f << ", "; },
                          [&](auto el) { std::cout << "(INDEX " << el.index() << "), "; }},
               get_operand_stack().get(i));
      }
      std::cout << " }\n";
   }

   inline uint32_t table_elem(uint32_t i) {
      return _mod->tables[0].table[i];
   }
   inline void push_operand(operand_stack_elem el) {
      get_operand_stack().push(std::move(el));
   }
   inline operand_stack_elem get_operand(uint32_t index) const {
      return get_operand_stack().get(_last_op_index + index);
   }
   inline void eat_operands(uint32_t index) {
      get_operand_stack().eat(index);
   }
   inline void compact_operand(uint32_t index) {
      get_operand_stack().compact(index);
   }
   inline void set_operand(uint32_t index, const operand_stack_elem& el) {
      get_operand_stack().set(_last_op_index + index, el);
   }
   inline uint32_t current_operands_index() const {
      return get_operand_stack().current_index();
   }
   inline void push_call(activation_frame&& el) {
      _as.push(std::move(el));
   }
   inline activation_frame pop_call() {
      return _as.pop();
   }
   inline uint32_t call_depth() const {
      return _as.size();
   }
   template <bool Should_Exit = false> inline void push_call(uint32_t index) {
      opcode* return_pc = static_cast<opcode*>(&_halt);
      if constexpr (!Should_Exit)
         return_pc = _state.pc + 1;

      _as.push(activation_frame{return_pc, _last_op_index});
      _last_op_index = get_operand_stack().size() - _mod->get_function_type(index).param_types.size();
   }

   inline void apply_pop_call(uint32_t num_locals, uint16_t return_count) {
      const auto& af = _as.pop();
      _state.pc = af.pc;
      _last_op_index = af.last_op_index;
      if (return_count)
         compact_operand(get_operand_stack().size() - num_locals - 1);
      else
         eat_operands(get_operand_stack().size() - num_locals);
   }
   inline operand_stack_elem pop_operand() {
      return get_operand_stack().pop();
   }
   inline operand_stack_elem& peek_operand(size_t i = 0) {
      return get_operand_stack().peek(i);
   }
   inline operand_stack_elem get_global(uint32_t index) {
      detail::check<exceptions::interpreter>((index < _mod->globals.size()), "global index out of range");
      detail::check<exceptions::interpreter>((index < _globals.size()),
                                             "index for _globals out of range in get_global for interpreter");
      const auto& gl = _mod->globals[index];
      switch (gl.type.content_type) {
      case types::i32:
         return i32_const_t{_globals[index].value.i32};
      case types::i64:
         return i64_const_t{_globals[index].value.i64};
      case types::f32:
         return f32_const_t{_globals[index].value.f32};
      case types::f64:
         return f64_const_t{_globals[index].value.f64};
      default:
         throw exceptions::interpreter{"invalid global type"};
      }
   }

   inline void set_global(uint32_t index, const operand_stack_elem& el) {
      detail::check<exceptions::interpreter>((index < _mod->globals.size()), "global index out of range");
      detail::check<exceptions::interpreter>((index < _globals.size()), "index for _globals out of range");
      auto& gl = _mod->globals[index];
      detail::check<exceptions::interpreter>((gl.type.mutability), "global is not mutable");
      visit(overloaded{[&](const i32_const_t& i) {
                          detail::check<exceptions::interpreter>((gl.type.content_type == types::i32),
                                                                 "expected i32 global type");
                          _globals[index].value.i32 = i.data.ui;
                       },
                       [&](const i64_const_t& i) {
                          detail::check<exceptions::interpreter>((gl.type.content_type == types::i64),
                                                                 "expected i64 global type");
                          _globals[index].value.i64 = i.data.ui;
                       },
                       [&](const f32_const_t& f) {
                          detail::check<exceptions::interpreter>((gl.type.content_type == types::f32),
                                                                 "expected f32 global type");
                          _globals[index].value.f32 = f.data.ui;
                       },
                       [&](const f64_const_t& f) {
                          detail::check<exceptions::interpreter>((gl.type.content_type == types::f64),
                                                                 "expected f64 global type");
                          _globals[index].value.f64 = f.data.ui;
                       },
                       [](auto) { throw exceptions::interpreter{"invalid global type"}; }},
            el);
   }

   inline bool is_true(const operand_stack_elem& el) {
      bool ret_val = false;
      visit(overloaded{[&](const i32_const_t& i32) { ret_val = i32.data.ui; },
                       [&](auto) { throw exceptions::invalid_element{"should be an i32 type"}; }},
            el);
      return ret_val;
   }

   inline void type_check(const func_type& ft) {
      for (uint32_t i = 0; i < ft.param_types.size(); i++) {
         const auto& op = peek_operand((ft.param_types.size() - 1) - i);
         visit(overloaded{[&](const i32_const_t&) {
                             detail::check<exceptions::interpreter>((ft.param_types[i] == types::i32),
                                                                    "function param type mismatch");
                          },
                          [&](const f32_const_t&) {
                             detail::check<exceptions::interpreter>((ft.param_types[i] == types::f32),
                                                                    "function param type mismatch");
                          },
                          [&](const i64_const_t&) {
                             detail::check<exceptions::interpreter>((ft.param_types[i] == types::i64),
                                                                    "function param type mismatch");
                          },
                          [&](const f64_const_t&) {
                             detail::check<exceptions::interpreter>((ft.param_types[i] == types::f64),
                                                                    "function param type mismatch");
                          },
                          [&](auto) { throw exceptions::interpreter{"function param invalid type"}; }},
               op);
      }
   }

   inline opcode* get_pc() const {
      return _state.pc;
   }
   inline void set_relative_pc(uint32_t pc_offset) {
      _state.pc = _mod->code[0].code + pc_offset;
   }
   inline void set_pc(opcode* pc) {
      _state.pc = pc;
   }
   inline void inc_pc(uint32_t offset = 1) {
      _state.pc += offset;
   }
   inline void exit(std::error_code err = std::error_code()) {
      _error_code = err;
      _state.pc = &_halt;
      _state.exiting = true;
   }

   inline void reset() {
      base_type::reset(*_mod);
      _state = execution_state{};
      get_operand_stack().eat(_state.os_index);
      _as.eat(_state.as_index);
   }

   template <typename Visitor, typename... Args>
   inline std::optional<operand_stack_elem> execute_func_table(host_type* host, Visitor&& visitor, uint32_t table_index,
                                                               Args&&... args) {
      return execute(host, std::forward<Visitor>(visitor), table_elem(table_index), std::forward<Args>(args)...);
   }

   template <typename Visitor, typename... Args>
   inline std::optional<operand_stack_elem> execute(host_type* host, Visitor&& visitor, const std::string_view func,
                                                    Args&&... args) {
      uint32_t func_index = _mod->get_exported_function(func);
      return execute(host, std::forward<Visitor>(visitor), func_index, std::forward<Args>(args)...);
   }

   template <typename Visitor, typename... Args> inline void execute_start(host_type* host, Visitor&& visitor) {
      if (_mod->start != std::numeric_limits<uint32_t>::max())
         execute(host, std::forward<Visitor>(visitor), _mod->start);
   }

   template <typename Visitor, typename... Args>
   inline std::optional<operand_stack_elem> execute(host_type* host, Visitor&& visitor, uint32_t func_index,
                                                    Args&&... args) {
      detail::check<exceptions::interpreter>((func_index < std::numeric_limits<uint32_t>::max()),
                                             "cannot execute function, function not found");

      auto last_last_op_index = _last_op_index;

      // save the state of the original calling context
      execution_state saved_state = _state;

      _state.host = host;
      _state.as_index = _as.size();
      _state.os_index = get_operand_stack().size();

      auto cleanup = scope_guard([&]() {
         get_operand_stack().eat(_state.os_index);
         _as.eat(_state.as_index);
         _state = saved_state;

         _last_op_index = last_last_op_index;
      });

      this->type_check_args(_mod->get_function_type(func_index), std::forward<Args>(args)...); // args not modified
      push_args(std::forward<Args>(args)...);
      push_call<true>(func_index);

      if (func_index < _mod->get_imported_functions_size()) {
         _rhf(_state.host, get_interface(), _mod->import_functions[func_index]);
      } else {
         _state.pc = _mod->get_function_pc(func_index);
         setup_locals(func_index);
         ::forge::vm::wasm::invoke_with_signal_handler([&]() { execute(std::forward<Visitor>(visitor)); },
                                                       &handle_signal, _mod->allocator,
                                                       base_type::get_wasm_allocator());
      }

      if (_mod->get_function_type(func_index).return_count && !_state.exiting) {
         return pop_operand();
      } else {
         return {};
      }
   }

   inline void jump(uint32_t pop_info, uint32_t new_pc) {
      set_relative_pc(new_pc);
      if ((pop_info & 0x80000000u)) {
         const auto& op = pop_operand();
         eat_operands(get_operand_stack().size() - ((pop_info & 0x7FFFFFFFu) - 1));
         push_operand(op);
      } else {
         eat_operands(get_operand_stack().size() - pop_info);
      }
   }

   // This isn't async-signal-safe.  Cross fingers and hope for the best.
   // It's only used for profiling.
   int backtrace(void** data, int limit, void* uc) const {
      int out = 0;
      if (limit != 0) {
         data[out++] = _state.pc;
      }
      for (int i = 0; out < limit && i < _as.size(); ++i) {
         data[out++] = _as.get_back(i).pc;
      }
      return out;
   }

 private:
   template <typename... Args> void push_args(Args&&... args) {
      auto tc = detail::type_converter_t<Host>{_host, get_interface()};
      (void)tc;
      (..., push_operand(detail::resolve_result(tc, std::forward<Args>(args))));
   }

   inline void setup_locals(uint32_t index) {
      const auto& fn = _mod->code[index - _mod->get_imported_functions_size()];
      for (uint32_t i = 0; i < fn.locals.size(); i++) {
         for (uint32_t j = 0; j < fn.locals[i].count; j++)
            switch (fn.locals[i].type) {
            case types::i32:
               push_operand(i32_const_t{(uint32_t)0});
               break;
            case types::i64:
               push_operand(i64_const_t{(uint64_t)0});
               break;
            case types::f32:
               push_operand(f32_const_t{(uint32_t)0});
               break;
            case types::f64:
               push_operand(f64_const_t{(uint64_t)0});
               break;
            default:
               throw exceptions::interpreter{"invalid function param type"};
            }
      }
   }

#define CREATE_TABLE_ENTRY(NAME, CODE) &&ev_label_##NAME,
#define CREATE_LABEL(NAME, CODE)                                                                                       \
   ev_label_##NAME                                                                                                     \
       : std::forward<Visitor>(visitor)(ev_variant->template get<forge::vm::wasm::FORGE_VM_WASM_OPCODE_T(NAME)>());    \
   ev_variant = _state.pc;                                                                                             \
   goto* dispatch_table[ev_variant->index()];
#define CREATE_EXIT_LABEL(NAME, CODE) ev_label_##NAME : return;
#define CREATE_EMPTY_LABEL(NAME, CODE) ev_label_##NAME : throw exceptions::interpreter{"empty operand"};

   template <typename Visitor> void execute(Visitor&& visitor) {
      static void* dispatch_table[] = {
          FORGE_VM_WASM_CONTROL_FLOW_OPS(CREATE_TABLE_ENTRY) FORGE_VM_WASM_BR_TABLE_OP(CREATE_TABLE_ENTRY)
              FORGE_VM_WASM_RETURN_OP(CREATE_TABLE_ENTRY) FORGE_VM_WASM_CALL_OPS(CREATE_TABLE_ENTRY)
                  FORGE_VM_WASM_CALL_IMM_OPS(CREATE_TABLE_ENTRY) FORGE_VM_WASM_PARAMETRIC_OPS(CREATE_TABLE_ENTRY)
                      FORGE_VM_WASM_VARIABLE_ACCESS_OPS(CREATE_TABLE_ENTRY) FORGE_VM_WASM_MEMORY_OPS(CREATE_TABLE_ENTRY)
                          FORGE_VM_WASM_I32_CONSTANT_OPS(CREATE_TABLE_ENTRY)
                              FORGE_VM_WASM_I64_CONSTANT_OPS(CREATE_TABLE_ENTRY)
                                  FORGE_VM_WASM_F32_CONSTANT_OPS(CREATE_TABLE_ENTRY)
                                      FORGE_VM_WASM_F64_CONSTANT_OPS(CREATE_TABLE_ENTRY)
                                          FORGE_VM_WASM_COMPARISON_OPS(CREATE_TABLE_ENTRY)
                                              FORGE_VM_WASM_NUMERIC_OPS(CREATE_TABLE_ENTRY)
                                                  FORGE_VM_WASM_CONVERSION_OPS(CREATE_TABLE_ENTRY)
                                                      FORGE_VM_WASM_EXIT_OP(CREATE_TABLE_ENTRY)
                                                          FORGE_VM_WASM_EMPTY_OPS(CREATE_TABLE_ENTRY)
                                                              FORGE_VM_WASM_ERROR_OPS(CREATE_TABLE_ENTRY) &&
          __ev_last};
      auto* ev_variant = _state.pc;
      goto* dispatch_table[ev_variant->index()];
      while (1) {
         FORGE_VM_WASM_CONTROL_FLOW_OPS(CREATE_LABEL);
         FORGE_VM_WASM_BR_TABLE_OP(CREATE_LABEL);
         FORGE_VM_WASM_RETURN_OP(CREATE_LABEL);
         FORGE_VM_WASM_CALL_OPS(CREATE_LABEL);
         FORGE_VM_WASM_CALL_IMM_OPS(CREATE_LABEL);
         FORGE_VM_WASM_PARAMETRIC_OPS(CREATE_LABEL);
         FORGE_VM_WASM_VARIABLE_ACCESS_OPS(CREATE_LABEL);
         FORGE_VM_WASM_MEMORY_OPS(CREATE_LABEL);
         FORGE_VM_WASM_I32_CONSTANT_OPS(CREATE_LABEL);
         FORGE_VM_WASM_I64_CONSTANT_OPS(CREATE_LABEL);
         FORGE_VM_WASM_F32_CONSTANT_OPS(CREATE_LABEL);
         FORGE_VM_WASM_F64_CONSTANT_OPS(CREATE_LABEL);
         FORGE_VM_WASM_COMPARISON_OPS(CREATE_LABEL);
         FORGE_VM_WASM_NUMERIC_OPS(CREATE_LABEL);
         FORGE_VM_WASM_CONVERSION_OPS(CREATE_LABEL);
         FORGE_VM_WASM_EXIT_OP(CREATE_EXIT_LABEL);
         FORGE_VM_WASM_EMPTY_OPS(CREATE_EMPTY_LABEL);
         FORGE_VM_WASM_ERROR_OPS(CREATE_LABEL);
      __ev_last:
         throw exceptions::interpreter{"should never reach here"};
      }
   }

#undef CREATE_EMPTY_LABEL
#undef CREATE_LABEL
#undef CREATE_TABLE_ENTRY

   struct execution_state {
      host_type* host = nullptr;
      uint32_t as_index = 0;
      uint32_t os_index = 0;
      opcode* pc = nullptr;
      bool exiting = false;
   };

   bounded_allocator _base_allocator = {(constants::max_call_depth + 1) * sizeof(activation_frame)};
   execution_state _state;
   uint32_t _last_op_index = 0;
   call_stack _as = {_base_allocator};
   opcode _halt;
   host_type* _host = nullptr;
};
} // namespace forge::vm::wasm

namespace forge::vm::wasm {

class null_writer {
 public:
   struct branch_t {};
   struct label_t {};
   explicit null_writer(growable_allocator& alloc, std::size_t source_bytes, module& mod) {}
   void emit_unreachable() {}
   void emit_nop() {}
   label_t emit_end() {
      return {};
   }
   branch_t emit_return(uint32_t /*depth_change*/) {
      return {};
   }
   void emit_block() {}
   label_t emit_loop() {
      return {};
   }
   branch_t emit_if() {
      return {};
   }
   branch_t emit_else(branch_t /*if_loc*/) {
      return {};
   }
   branch_t emit_br(uint32_t /*depth_change*/) {
      return {};
   }
   branch_t emit_br_if(uint32_t /*depth_change*/) {
      return {};
   }
   struct br_table_parser {
      branch_t emit_case(uint32_t /*depth_change*/) {
         return {};
      }
      branch_t emit_default(uint32_t /*depth_change*/) {
         return {};
      }
   };
   br_table_parser emit_br_table(uint32_t /*table_size*/) {
      return {};
   }
   void emit_call(const func_type& /*ft*/, uint32_t /*funcnum*/) {}
   void emit_call_indirect(const func_type& /*ft*/, uint32_t /*functypeidx*/) {}

   void emit_drop() {}
   void emit_select() {}
   void emit_get_local(uint32_t /*localidx*/) {}
   void emit_set_local(uint32_t /*localidx*/) {}
   void emit_tee_local(uint32_t /*localidx*/) {}
   void emit_get_global(uint32_t /*localidx*/) {}
   void emit_set_global(uint32_t /*localidx*/) {}

   void emit_i32_load(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i64_load(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_f32_load(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_f64_load(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i32_load8_s(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i32_load16_s(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i32_load8_u(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i32_load16_u(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i64_load8_s(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i64_load16_s(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i64_load32_s(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i64_load8_u(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i64_load16_u(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i64_load32_u(uint32_t /*offset*/, uint32_t /*alignment*/) {}

   void emit_i32_store(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i64_store(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_f32_store(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_f64_store(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i32_store8(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i32_store16(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i64_store8(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i64_store16(uint32_t /*offset*/, uint32_t /*alignment*/) {}
   void emit_i64_store32(uint32_t /*offset*/, uint32_t /*alignment*/) {}

   void emit_current_memory() {}
   void emit_grow_memory() {}
   void emit_i32_const(uint32_t /*value*/) {}
   void emit_i64_const(uint64_t /*value*/) {}
   void emit_f32_const(float /*value*/) {}
   void emit_f64_const(double /*value*/) {}

   void emit_i32_eqz() {}
   void emit_i32_eq() {}
   void emit_i32_ne() {}
   void emit_i32_lt_s() {}
   void emit_i32_lt_u() {}
   void emit_i32_gt_s() {}
   void emit_i32_gt_u() {}
   void emit_i32_le_s() {}
   void emit_i32_le_u() {}
   void emit_i32_ge_s() {}
   void emit_i32_ge_u() {}

   void emit_i64_eqz() {}
   void emit_i64_eq() {}
   void emit_i64_ne() {}
   void emit_i64_lt_s() {}
   void emit_i64_lt_u() {}
   void emit_i64_gt_s() {}
   void emit_i64_gt_u() {}
   void emit_i64_le_s() {}
   void emit_i64_le_u() {}
   void emit_i64_ge_s() {}
   void emit_i64_ge_u() {}

   void emit_f32_eq() {}
   void emit_f32_ne() {}
   void emit_f32_lt() {}
   void emit_f32_gt() {}
   void emit_f32_le() {}
   void emit_f32_ge() {}

   void emit_f64_eq() {}
   void emit_f64_ne() {}
   void emit_f64_lt() {}
   void emit_f64_gt() {}
   void emit_f64_le() {}
   void emit_f64_ge() {}

   void emit_i32_clz() {}
   void emit_i32_ctz() {}
   void emit_i32_popcnt() {}
   void emit_i32_add() {}
   void emit_i32_sub() {}
   void emit_i32_mul() {}
   void emit_i32_div_s() {}
   void emit_i32_div_u() {}
   void emit_i32_rem_s() {}
   void emit_i32_rem_u() {}
   void emit_i32_and() {}
   void emit_i32_or() {}
   void emit_i32_xor() {}
   void emit_i32_shl() {}
   void emit_i32_shr_s() {}
   void emit_i32_shr_u() {}
   void emit_i32_rotl() {}
   void emit_i32_rotr() {}

   void emit_i64_clz() {}
   void emit_i64_ctz() {}
   void emit_i64_popcnt() {}
   void emit_i64_add() {}
   void emit_i64_sub() {}
   void emit_i64_mul() {}
   void emit_i64_div_s() {}
   void emit_i64_div_u() {}
   void emit_i64_rem_s() {}
   void emit_i64_rem_u() {}
   void emit_i64_and() {}
   void emit_i64_or() {}
   void emit_i64_xor() {}
   void emit_i64_shl() {}
   void emit_i64_shr_s() {}
   void emit_i64_shr_u() {}
   void emit_i64_rotl() {}
   void emit_i64_rotr() {}

   void emit_f32_abs() {}
   void emit_f32_neg() {}
   void emit_f32_ceil() {}
   void emit_f32_floor() {}
   void emit_f32_trunc() {}
   void emit_f32_nearest() {}
   void emit_f32_sqrt() {}
   void emit_f32_add() {}
   void emit_f32_sub() {}
   void emit_f32_mul() {}
   void emit_f32_div() {}
   void emit_f32_min() {}
   void emit_f32_max() {}
   void emit_f32_copysign() {}

   void emit_f64_abs() {}
   void emit_f64_neg() {}
   void emit_f64_ceil() {}
   void emit_f64_floor() {}
   void emit_f64_trunc() {}
   void emit_f64_nearest() {}
   void emit_f64_sqrt() {}
   void emit_f64_add() {}
   void emit_f64_sub() {}
   void emit_f64_mul() {}
   void emit_f64_div() {}
   void emit_f64_min() {}
   void emit_f64_max() {}
   void emit_f64_copysign() {}

   void emit_i32_wrap_i64() {}
   void emit_i32_trunc_s_f32() {}
   void emit_i32_trunc_u_f32() {}
   void emit_i32_trunc_s_f64() {}
   void emit_i32_trunc_u_f64() {}
   void emit_i64_extend_s_i32() {}
   void emit_i64_extend_u_i32() {}
   void emit_i64_trunc_s_f32() {}
   void emit_i64_trunc_u_f32() {}
   void emit_i64_trunc_s_f64() {}
   void emit_i64_trunc_u_f64() {}
   void emit_f32_convert_s_i32() {}
   void emit_f32_convert_u_i32() {}
   void emit_f32_convert_s_i64() {}
   void emit_f32_convert_u_i64() {}
   void emit_f32_demote_f64() {}
   void emit_f64_convert_s_i32() {}
   void emit_f64_convert_u_i32() {}
   void emit_f64_convert_s_i64() {}
   void emit_f64_convert_u_i64() {}
   void emit_f64_promote_f32() {}
   void emit_i32_reinterpret_f32() {}
   void emit_i64_reinterpret_f64() {}
   void emit_f32_reinterpret_i32() {}
   void emit_f64_reinterpret_i64() {}

   void fix_branch(branch_t, label_t) {}
   void emit_prologue(const func_type& /*ft*/, const guarded_vector<local_entry>& /*locals*/, uint32_t /*idx*/) {}
   void emit_epilogue(const func_type& /*ft*/, const guarded_vector<local_entry>& /*locals*/, uint32_t /*idx*/) {}
   void finalize(function_body& /*body*/) {}

   const void* get_addr() const {
      return nullptr;
   }
   const void* get_base_addr() const {
      return nullptr;
   }
};

} // namespace forge::vm::wasm

namespace forge::vm::wasm {
template <size_t N> inline size_t constexpr bytes_needed() {
   if constexpr (N == 1 || N == 7)
      return 1;
   else if constexpr (N == 32)
      return 5;
   else
      return 10;
}

template <size_t N> class varuint {
 public:
   static_assert(N == 1 || N == 7 || N == 32, "N not valid");

   inline constexpr explicit varuint(bool v) {
      from(v);
   }
   inline constexpr explicit varuint(uint8_t v) {
      from(v);
   }
   inline constexpr explicit varuint(uint32_t v) {
      from(v);
   }
   inline constexpr varuint(guarded_ptr<uint8_t>& code) {
      from(code);
   }

   inline constexpr void from(bool v) {
      storage[0] = v;
   }
   inline constexpr void from(uint8_t v) {
      storage[0] = v & 0x7f;
   }
   inline constexpr void from(uint32_t v) {
      bytes_used = 0;

#ifdef __clang__
#pragma unroll
#elif defined(__GNUC__)
#pragma GCC unroll 5
#endif
      for (; bytes_used < bytes_needed<N>(); bytes_used++) {
         storage[bytes_used] = v & 0x7f;
         v >>= 7;
         if (v != 0)
            storage[bytes_used] |= 0x80;
         else
            break;
      }
      bytes_used++;
   }

   inline constexpr void from(guarded_ptr<uint8_t>& code) {
      uint8_t cnt = 0;
      for (;; cnt++) {
         detail::check<exceptions::interpreter>((cnt < bytes_needed<N>()), "incorrect leb128 encoding");
         detail::check<exceptions::interpreter>((code.offset() + cnt < code.bounds()), "pointer out of bounds");
         storage[cnt] = code[cnt];
         if ((storage[cnt] & 0x80) == 0) {
            if (static_cast<size_t>(cnt + 1) == bytes_needed<N>()) {
               uint8_t mask = static_cast<uint8_t>(~(uint32_t)0 << uint32_t(N - 7 * (bytes_needed<N>() - 1))) & 0x7F;
               detail::check<exceptions::parse>(((mask & storage[cnt]) == 0),
                                                "unused bits of unsigned leb128 must be 0");
            }
            break;
         }
      }
      code += cnt + 1;
      bytes_used = cnt + 1;
   }

   size_t size() const {
      return bytes_used;
   }

   template <size_t M = N, typename = typename std::enable_if_t<M == 1, int>> inline constexpr bool to() {
      return storage[0];
   }

   template <size_t M = N, typename = typename std::enable_if_t<M == 7, int>> inline constexpr uint8_t to() {
      return storage[0] & 0x7f;
   }

   template <size_t M = N, typename = typename std::enable_if_t<M == 32, int>> inline constexpr uint32_t to() {
      uint32_t ret = 0;

#ifdef __clang__
#pragma unroll
#elif defined(__GNUC__)
#pragma GCC unroll 5
#endif
      for (int i = bytes_used - 1; i >= 0; i--) {
         ret <<= 7;
         ret |= storage[i] & 0x7f;
      }
      return ret;
   }

   void print() const {
      for (int i = 0; i < bytes_used; i++) {
         std::cout << std::hex << "0x" << (int)storage[i] << ' ';
      }
      std::cout << std::endl;
   }

 private:
   std::array<uint8_t, bytes_needed<N>()> storage;
   uint8_t bytes_used = bytes_needed<N>();
};

template <size_t N> class varint {
 public:
   static_assert(N == 7 || N == 32 || N == 64, "N not valid");

   inline constexpr explicit varint(int8_t v) {
      from(v);
   }
   inline constexpr explicit varint(int32_t v) {
      from(v);
   }
   inline constexpr explicit varint(int64_t v) {
      from(v);
   }
   inline constexpr varint(guarded_ptr<uint8_t>& code) {
      from(code);
   }

   inline constexpr void from(int8_t v) {
      static_assert(N >= 7, "cant use this constructor with N < 7");
      storage[0] = v & 0x7f;
   }
   inline constexpr void from(int32_t v) {
      static_assert(N >= 32, "cant use this constructor with N < 32");
      _from(v);
   }
   inline constexpr void from(int64_t v) {
      static_assert(N >= 64, "cant use this constructor with N < 32");
      _from(v);
   }

   inline constexpr void from(guarded_ptr<uint8_t>& code) {
      uint8_t cnt = 0;
      for (;; cnt++) {
         detail::check<exceptions::interpreter>((cnt < bytes_needed<N>()), "incorrect leb128 encoding");
         detail::check<exceptions::interpreter>((code.offset() + cnt < code.bounds()), "pointer out of bounds");
         storage[cnt] = code[cnt];
         if ((storage[cnt] & 0x80) == 0) {
            if (static_cast<size_t>(cnt + 1) == bytes_needed<N>()) {
               uint32_t offset = N - 7 * (bytes_needed<N>() - 1);
               uint8_t mask = static_cast<uint8_t>(~(uint32_t)0 << offset) & 0x7F;
               uint8_t expected = (storage[cnt] & (uint32_t(1) << uint32_t(offset - 1))) ? mask : 0;
               detail::check<exceptions::parse>(((mask & storage[cnt]) == expected),
                                                "unused bits of signed leb128 must be the same as the sign bit");
            }
            break;
         }
      }
      code += cnt + 1;
      bytes_used = cnt + 1;
   }

   size_t size() const {
      return bytes_used;
   }

   template <size_t M = N, typename = typename std::enable_if_t<M == 1, int>> inline constexpr bool to() {
      return storage[0];
   }

   template <size_t M = N, typename = typename std::enable_if_t<M == 7, int>> inline constexpr int8_t to() {
      if (storage[0] & 0x40)
         return storage[0] | (~0u << 7);
      return storage[0];
   }

   template <size_t M = N, typename = typename std::enable_if_t<M == 32, int>> inline constexpr int32_t to() {
      return _to<int32_t>();
   }

   template <size_t M = N, typename = typename std::enable_if_t<M == 64, int>> inline constexpr int64_t to() {
      return _to<int64_t>();
   }

   void print() const {
      for (int i = 0; i < bytes_used; i++) {
         std::cout << std::hex << "0x" << (int)storage[i] << ' ';
      }
      std::cout << std::endl;
   }

 private:
   template <typename T> inline constexpr void _from(T v) {
      bytes_used = 0;

#ifdef __clang__
#pragma unroll
#elif defined(__GNUC__)
#pragma GCC unroll 5
#endif
      for (; bytes_used < bytes_needed<N>(); bytes_used++) {
         storage[bytes_used] = v & 0x7f;
         v >>= 7;
         if ((v == -1 && (storage[bytes_used] & 0x40)) || (v == 0 && !(storage[bytes_used] & 0x40)))
            break;
         storage[bytes_used] |= 0x80;
      }
      bytes_used++;
   }

   template <typename T> inline constexpr T _to() {
      typename std::make_unsigned<T>::type ret = 0;

#ifdef __clang__
#pragma unroll
#elif defined(__GNUC__)
#pragma GCC unroll 5
#endif
      for (int i = bytes_used - 1; i >= 0; i--) {
         ret <<= 7;
         ret |= storage[i] & 0x7f;
      }
      if (bytes_used >= 1 && bytes_used < bytes_needed<N>()) {
         size_t shift = ((bytes_used) * 7);
         if (storage[bytes_used - 1] & 0x40)
            ret |= (-1ull) << shift;
      }
      return *(T*)&ret;
   }

   std::array<uint8_t, bytes_needed<N>()> storage;
   uint8_t bytes_used = bytes_needed<N>();
};

} // namespace forge::vm::wasm

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

namespace forge::vm::wasm {

namespace detail {

inline constexpr unsigned get_size_for_type(uint8_t type) {
   switch (type) {
   case types::i32:
   case types::f32:
      return 4;
   case types::i64:
   case types::f64:
      return 8;
   default:
      return 0;
   }
}

template <typename Options, typename Enable = void> struct max_mutable_globals_checker {
   constexpr void on_mutable_global(const Options&, uint8_t) {}
};

template <typename Options> using max_mutable_globals_t = decltype(std::declval<Options>().max_mutable_global_bytes);

template <typename Options> struct max_mutable_globals_checker<Options, std::void_t<max_mutable_globals_t<Options>>> {
   static_assert(std::is_unsigned_v<std::decay_t<max_mutable_globals_t<Options>>>,
                 "max_mutable_globals must be an unsigned integer type");
   void on_mutable_global(const Options& options, uint8_t type) {
      unsigned size = get_size_for_type(type);
      _counter += size;
      detail::check<exceptions::parse>((_counter <= options.max_mutable_global_bytes && _counter >= size),
                                       "mutable globals exceeded limit");
   }
   std::decay_t<max_mutable_globals_t<Options>> _counter = 0;
};

#define PARSER_OPTION(name, default_, type)                                                                            \
   template <typename Options> type get_##name(const Options& options, long) {                                         \
      (void)options;                                                                                                   \
      return default_;                                                                                                 \
   }                                                                                                                   \
   template <typename Options> auto get_##name(const Options& options, int) -> decltype(options.name) {                \
      return options.name;                                                                                             \
   }                                                                                                                   \
   template <typename Options> type get_##name(const Options& options) {                                               \
      return detail::get_##name(options, 0);                                                                           \
   }

#define MAX_ELEMENTS(name, default_) PARSER_OPTION(name, default_, std::uint32_t)

MAX_ELEMENTS(max_table_elements, 0xFFFFFFFFu)
MAX_ELEMENTS(max_section_elements, 0xFFFFFFFFu)

MAX_ELEMENTS(max_type_section_elements, detail::get_max_section_elements(options))
MAX_ELEMENTS(max_import_section_elements, detail::get_max_section_elements(options))
MAX_ELEMENTS(max_function_section_elements, detail::get_max_section_elements(options))
MAX_ELEMENTS(max_global_section_elements, detail::get_max_section_elements(options))
MAX_ELEMENTS(max_export_section_elements, detail::get_max_section_elements(options))
MAX_ELEMENTS(max_element_section_elements, detail::get_max_section_elements(options))
MAX_ELEMENTS(max_data_section_elements, detail::get_max_section_elements(options))

MAX_ELEMENTS(max_element_segment_elements, 0xFFFFFFFFu)
MAX_ELEMENTS(max_data_segment_bytes, 0xFFFFFFFFu)

PARSER_OPTION(max_linear_memory_init, 0xFFFFFFFFFFFFFFFFu, std::uint64_t)
PARSER_OPTION(max_func_local_bytes_flags, max_func_local_bytes_flags_t::locals | max_func_local_bytes_flags_t::stack,
              max_func_local_bytes_flags_t);

template <typename Options, typename Enable = void> struct max_func_local_bytes_checker {
   explicit max_func_local_bytes_checker(const Options&, const func_type& /*ft*/) {}
   void on_local(const Options&, std::uint8_t, const std::uint32_t) {}
   void push_stack(const Options& /*options*/, std::uint8_t /*type*/) {}
   void pop_stack(std::uint8_t /*type*/) {}
   void push_unreachable() {}
   void pop_unreachable() {}
   static constexpr bool is_defined = false;
};
template <typename Options>
struct max_func_local_bytes_checker<Options, std::void_t<decltype(std::declval<Options>().max_func_local_bytes)>> {
   explicit max_func_local_bytes_checker(const Options& options, const func_type& ft) {
      if ((detail::get_max_func_local_bytes_flags(options) & max_func_local_bytes_flags_t::params) !=
          (max_func_local_bytes_flags_t)0) {
         for (std::uint32_t i = 0; i < ft.param_types.size(); ++i) {
            on_type(options, ft.param_types.at(i));
         }
      }
   }
   void on_type(const Options& options, std::uint8_t type) {
      unsigned size = get_size_for_type(type);
      _count += size;
      detail::check<exceptions::parse>((_count <= options.max_func_local_bytes && _count >= size),
                                       "local variable limit exceeded");
   }
   void on_local(const Options& options, std::uint8_t type, std::uint32_t count) {
      if ((detail::get_max_func_local_bytes_flags(options) & max_func_local_bytes_flags_t::locals) !=
          (max_func_local_bytes_flags_t)0) {
         uint64_t size = get_size_for_type(type);
         size *= count;
         _count += size;
         detail::check<exceptions::parse>((_count <= options.max_func_local_bytes && _count >= size),
                                          "local variable limit exceeded");
      }
   }
   std::decay_t<decltype(std::declval<Options>().max_func_local_bytes)> _count = 0;
   static constexpr bool is_defined = true;
};
template <typename Options>
constexpr auto get_max_func_local_bytes_no_stack_c(int)
    -> std::enable_if_t<std::is_pointer_v<decltype(&Options::max_func_local_bytes_flags)>, bool> {
   return (Options::max_func_local_bytes_flags & max_func_local_bytes_flags_t::stack) ==
          (max_func_local_bytes_flags_t)0;
}
template <typename Options> constexpr auto get_max_func_local_bytes_no_stack_c(long) -> bool {
   return false;
}

template <typename Options, typename Enable = void>
struct max_func_local_bytes_stack_checker : max_func_local_bytes_checker<Options> {
   explicit constexpr max_func_local_bytes_stack_checker(const max_func_local_bytes_checker<Options>& base)
       : max_func_local_bytes_checker<Options>(base) {}
   void push_stack(const Options& options, std::uint8_t type) {
      if (unreachable_depth == 0 && (detail::get_max_func_local_bytes_flags(options) &
                                     max_func_local_bytes_flags_t::stack) != (max_func_local_bytes_flags_t)0) {
         this->on_type(options, type);
      }
   }
   void pop_stack(const Options& options, std::uint8_t type) {
      if (unreachable_depth == 0 && (detail::get_max_func_local_bytes_flags(options) &
                                     max_func_local_bytes_flags_t::stack) != (max_func_local_bytes_flags_t)0) {
         this->_count -= get_size_for_type(type);
      }
   }
   void push_unreachable() {
      ++unreachable_depth;
   }
   void pop_unreachable() {
      --unreachable_depth;
   }
   std::uint32_t unreachable_depth = 0;
};
template <typename Options>
struct max_func_local_bytes_stack_checker<Options,
                                          std::enable_if_t<!max_func_local_bytes_checker<Options>::is_defined ||
                                                           get_max_func_local_bytes_no_stack_c<Options>(0)>> {
   explicit constexpr max_func_local_bytes_stack_checker(const max_func_local_bytes_checker<Options>&) {}
   void push_stack(const Options& /*options*/, std::uint8_t /*type*/) {}
   void pop_stack(const Options& /*options*/, std::uint8_t /*type*/) {}
   void push_unreachable() {}
   void pop_unreachable() {}
};

MAX_ELEMENTS(max_local_sets, 0xFFFFFFFFu)
MAX_ELEMENTS(max_nested_structures, 0xFFFFFFFFu)
MAX_ELEMENTS(max_br_table_elements, 0xFFFFFFFFu)

// Matches the donor chain nested-control validation.
template <typename Options, typename Enable = void> struct max_control_depth_checker {
   void on_control(const Options&) {}
   void on_end(const Options&) {}
};
template <typename Options>
struct max_control_depth_checker<Options, std::void_t<decltype(std::declval<Options>().max_control_depth)>> {
   void on_control(const Options& options) {
      ++_count;
      detail::check<exceptions::parse>((_count <= options.max_control_depth), "Nested depth exceeded");
   }
   void on_end(const Options& options) {
      if (_count == 0)
         ++_count;
      else
         --_count;
   }
   std::decay_t<decltype(std::declval<Options>().max_control_depth)> _count = 0;
};

MAX_ELEMENTS(max_symbol_bytes, 0xFFFFFFFFu)
MAX_ELEMENTS(max_memory_offset, 0xFFFFFFFFu)
MAX_ELEMENTS(max_code_bytes, 0xFFFFFFFFu)
MAX_ELEMENTS(max_pages, 0xFFFFFFFFu)
MAX_ELEMENTS(max_call_depth, 251)

PARSER_OPTION(forbid_export_mutable_globals, false, bool);
PARSER_OPTION(allow_code_after_function_end, false, bool);
PARSER_OPTION(allow_u32_limits_flags, false, bool);
PARSER_OPTION(allow_invalid_empty_local_set, false, bool);

PARSER_OPTION(allow_zero_blocktype, false, bool)

PARSER_OPTION(parse_custom_section_name, false, bool);

#undef MAX_ELEMENTS
#undef PARSER_OPTION

} // namespace detail

template <typename Writer, typename Options = default_options, typename DebugInfo = null_debug_info>
class binary_parser {
 public:
   explicit binary_parser(growable_allocator& alloc, const Options& options = Options{})
       : _allocator(alloc), _options(options) {}

   template <typename T> using vec = guarded_vector<T>;

   static inline uint8_t parse_varuint1(wasm_code_ptr& code) {
      return varuint<1>(code).to();
   }

   static inline uint8_t parse_varuint7(wasm_code_ptr& code) {
      return varuint<7>(code).to();
   }

   static inline uint32_t parse_varuint32(wasm_code_ptr& code) {
      return varuint<32>(code).to();
   }

   static inline int8_t parse_varint7(wasm_code_ptr& code) {
      return varint<7>(code).to();
   }

   static inline int32_t parse_varint32(wasm_code_ptr& code) {
      return varint<32>(code).to();
   }

   static inline int64_t parse_varint64(wasm_code_ptr& code) {
      return varint<64>(code).to();
   }

   int validate_utf8_code_point(wasm_code_ptr& code) {
      unsigned char ch = *code++;
      if (ch < 0x80) {
         return 1;
      } else if (ch < 0xE0) {
         detail::check<exceptions::parse>(((ch & 0xC0) == 0xC0), "invalid utf8 encoding");
         unsigned char b2 = *code++;
         detail::check<exceptions::parse>(((b2 & 0xC0) == 0x80), "invalid utf8 encoding");
         uint32_t code_point = (static_cast<uint32_t>(ch - 0xC0u) << 6u) + (static_cast<uint32_t>(b2 - 0x80u));
         detail::check<exceptions::parse>((0x80 <= code_point && code_point < 0x800), "invalid utf8 encoding");
         return 2;
      } else if (ch < 0xF0) {
         unsigned char b2 = *code++;
         detail::check<exceptions::parse>(((b2 & 0xC0) == 0x80), "invalid utf8 encoding");
         unsigned char b3 = *code++;
         detail::check<exceptions::parse>(((b3 & 0xC0) == 0x80), "invalid utf8 encoding");
         uint32_t code_point = (static_cast<uint32_t>(ch - 0xE0u) << 12u) + (static_cast<uint32_t>(b2 - 0x80u) << 6u) +
                               (static_cast<uint32_t>(b3 - 0x80u));
         detail::check<exceptions::parse>(
             ((0x800 <= code_point && code_point < 0xD800) || (0xE000 <= code_point && code_point < 0x10000)),
             "invalid utf8 encoding");
         return 3;
      } else if (ch < 0xF8) {
         unsigned char b2 = *code++;
         detail::check<exceptions::parse>(((b2 & 0xC0) == 0x80), "invalid utf8 encoding");
         unsigned char b3 = *code++;
         detail::check<exceptions::parse>(((b3 & 0xC0) == 0x80), "invalid utf8 encoding");
         unsigned char b4 = *code++;
         detail::check<exceptions::parse>(((b4 & 0xC0) == 0x80), "invalid utf8 encoding");
         uint32_t code_point = (static_cast<uint32_t>(ch - 0xF0u) << 18u) + (static_cast<uint32_t>(b2 - 0x80u) << 12u) +
                               (static_cast<uint32_t>(b3 - 0x80u) << 6u) + (static_cast<uint32_t>(b4 - 0x80u));
         detail::check<exceptions::parse>(((0x10000 <= code_point && code_point < 0x110000)), "invalid utf8 encoding");
         return 4;
      }
      detail::fail<exceptions::parse>("invalid utf8 encoding");
   }

   void validate_utf8_string(wasm_code_ptr& code, uint32_t bytes) {
      while (bytes != 0) {
         bytes -= validate_utf8_code_point(code);
      }
   }

   guarded_vector<uint8_t> parse_utf8_string(wasm_code_ptr& code, std::uint32_t max_size) {
      auto len = parse_varuint32(code);
      detail::check<exceptions::parse>((len <= max_size), "name too long");
      auto guard = code.scoped_shrink_bounds(len);
      auto result = guarded_vector<uint8_t>{_allocator, len};
      result.copy(code.raw(), len);
      validate_utf8_string(code, len);
      return result;
   }

   template <typename T> T parse_raw(wasm_code_ptr& code) {
      static_assert(std::is_arithmetic_v<T>, "Can only read builtin types");
      auto guard = code.scoped_shrink_bounds(sizeof(T));
      T result;
      memcpy(&result, code.raw(), sizeof(T));
      code += sizeof(T);
      return result;
   }

   inline module& parse_module(wasm_code& code, module& mod, DebugInfo& debug) {
      wasm_code_ptr cp(code.data(), code.size());
      parse_module(cp, code.size(), mod, debug);
      return mod;
   }

   inline module& parse_module2(wasm_code_ptr& code_ptr, size_t sz, module& mod, DebugInfo& debug) {
      parse_module(code_ptr, sz, mod, debug);
      return mod;
   }

   void parse_module(wasm_code_ptr& code_ptr, size_t sz, module& mod, DebugInfo& debug) {
      _mod = &mod;
      detail::check<exceptions::parse>((parse_magic(code_ptr) == constants::magic), "magic number did not match");
      detail::check<exceptions::parse>((parse_version(code_ptr) == constants::version), "version number did not match");
      uint8_t highest_section_id = 0;
      for (;;) {
         if (code_ptr.offset() == sz)
            break;
         auto id = parse_section_id(code_ptr);
         auto len = parse_section_payload_len(code_ptr);

         detail::check<exceptions::parse>((id == 0 || id > highest_section_id), "section out of order");
         highest_section_id = std::max(highest_section_id, id);

         auto section_guard = code_ptr.scoped_consume_items(len);

         switch (id) {
         case section_id::custom_section:
            parse_custom(code_ptr);
            break;
         case section_id::type_section:
            parse_section<section_id::type_section>(code_ptr, mod.types);
            break;
         case section_id::import_section:
            parse_section<section_id::import_section>(code_ptr, mod.imports);
            break;
         case section_id::function_section:
            parse_section<section_id::function_section>(code_ptr, mod.functions);
            mod.normalize_types();
            break;
         case section_id::table_section:
            parse_section<section_id::table_section>(code_ptr, mod.tables);
            break;
         case section_id::memory_section:
            parse_section<section_id::memory_section>(code_ptr, mod.memories);
            break;
         case section_id::global_section:
            parse_section<section_id::global_section>(code_ptr, mod.globals);
            break;
         case section_id::export_section:
            parse_section<section_id::export_section>(code_ptr, mod.exports);
            validate_exports();
            break;
         case section_id::start_section:
            parse_section<section_id::start_section>(code_ptr, mod.start);
            break;
         case section_id::element_section:
            parse_section<section_id::element_section>(code_ptr, mod.elements);
            break;
         case section_id::code_section:
            parse_section<section_id::code_section>(code_ptr, mod.code);
            break;
         case section_id::data_section:
            parse_section<section_id::data_section>(code_ptr, mod.data);
            break;
         default:
            detail::fail<exceptions::parse>("error invalid section id");
         }
      }
      detail::check<exceptions::parse>((_mod->code.size() == _mod->functions.size()),
                                       "code section must have the same size as the function section");

      debug.set(std::move(imap));
      debug.relocate(_allocator.get_code_start());
   }

   inline uint32_t parse_magic(wasm_code_ptr& code) {
      return parse_raw<uint32_t>(code);
   }
   inline uint32_t parse_version(wasm_code_ptr& code) {
      return parse_raw<uint32_t>(code);
   }
   inline uint8_t parse_section_id(wasm_code_ptr& code) {
      return *code++;
   }
   inline uint32_t parse_section_payload_len(wasm_code_ptr& code) {
      return parse_varuint32(code);
   }

   inline void parse_custom(wasm_code_ptr& code) {
      auto section_name = parse_utf8_string(code, 0xFFFFFFFFu); // ignored, but needs to be validated
      if (detail::get_parse_custom_section_name(_options) && section_name.size() == 4 &&
          std::memcmp(section_name.raw(), "name", 4) == 0) {
         parse_name_section(code);
      } else {
         // skip to the end of the section
         code += code.bounds() - code.offset();
      }
   }

   inline void parse_name_map(wasm_code_ptr& code, guarded_vector<name_assoc>& map) {
      for (uint32_t i = 0; i < map.size(); ++i) {
         map[i].idx = parse_varuint32(code);
         map[i].name = parse_utf8_string(code, 0xFFFFFFFFu);
      }
   }

   inline void parse_name_section(wasm_code_ptr& code) {
      _mod->names = _allocator.alloc<name_section>(1);
      new (_mod->names) name_section;
      if (code.bounds() == code.offset())
         return;
      if (*code == 0) {
         ++code;
         auto subsection_guard = code.scoped_consume_items(parse_varuint32(code));
         _mod->names->module_name = _allocator.alloc<guarded_vector<uint8_t>>(1);
         new (_mod->names->module_name) guarded_vector<uint8_t>(parse_utf8_string(code, 0xFFFFFFFFu));
      }
      if (code.bounds() == code.offset())
         return;
      if (*code == 1) {
         ++code;
         auto subsection_guard = code.scoped_consume_items(parse_varuint32(code));
         uint32_t size = parse_varuint32(code);
         _mod->names->function_names = _allocator.alloc<guarded_vector<name_assoc>>(1);
         new (_mod->names->function_names) guarded_vector<name_assoc>(_allocator, size);
         parse_name_map(code, *_mod->names->function_names);
      }
      if (code.bounds() == code.offset())
         return;
      if (*code == 2) {
         ++code;
         auto subsection_guard = code.scoped_consume_items(parse_varuint32(code));
         uint32_t size = parse_varuint32(code);
         _mod->names->local_names = _allocator.alloc<guarded_vector<indirect_name_assoc>>(1);
         new (_mod->names->local_names) guarded_vector<indirect_name_assoc>(_allocator, size);
         for (uint32_t i = 0; i < size; ++i) {
            auto& [idx, namemap] = (*_mod->names->local_names)[i];
            idx = parse_varuint32(code);
            uint32_t local_size = parse_varuint32(code);
            namemap = guarded_vector<name_assoc>(_allocator, local_size);
            parse_name_map(code, namemap);
         }
      }
      if (code.bounds() == code.offset())
         return;
      detail::fail<exceptions::parse>("Invalid subsection Id");
   }

   void parse_import_entry(wasm_code_ptr& code, import_entry& entry) {
      entry.module_str = parse_utf8_string(code, detail::get_max_symbol_bytes(_options));
      entry.field_str = parse_utf8_string(code, detail::get_max_symbol_bytes(_options));
      entry.kind = (external_kind)(*code++);
      auto type = parse_varuint32(code);
      switch ((uint8_t)entry.kind) {
      case external_kind::Function:
         entry.type.func_t = type;
         detail::check<exceptions::parse>((type < _mod->types.size()), "Invalid function type");
         break;
      default:
         detail::fail<exceptions::unsupported_import>("only function imports are supported");
      }
   }

   uint8_t parse_flags(wasm_code_ptr& code) {
      if (detail::get_allow_u32_limits_flags(_options)) {
         return parse_varuint32(code) & 0x1;
      } else {
         detail::check<exceptions::parse>((*code == 0x0 || *code == 0x1), "invalid flags");
         return *code++;
      }
   }

   void parse_table_type(wasm_code_ptr& code, table_type& tt) {
      tt.element_type = *code++;
      detail::check<exceptions::parse>((tt.element_type == types::anyfunc), "table must have type anyfunc");
      tt.limits.flags = parse_flags(code);
      tt.limits.initial = parse_varuint32(code);
      if (tt.limits.flags) {
         tt.limits.maximum = parse_varuint32(code);
         detail::check<exceptions::parse>((tt.limits.initial <= tt.limits.maximum),
                                          "table max size less than min size");
      }
      detail::check<exceptions::parse>((tt.limits.initial <= detail::get_max_table_elements(_options)),
                                       "table size exceeds limit");
      tt.table = decltype(tt.table){_allocator, tt.limits.initial};
      for (uint32_t i = 0; i < tt.limits.initial; i++)
         tt.table[i] = std::numeric_limits<uint32_t>::max();
   }

   void parse_global_variable(wasm_code_ptr& code, global_variable& gv) {
      uint8_t ct = *code++;
      gv.type.content_type = ct;
      detail::check<exceptions::parse>((ct == types::i32 || ct == types::i64 || ct == types::f32 || ct == types::f64),
                                       "invalid global content type");

      gv.type.mutability = parse_varuint1(code);
      if (gv.type.mutability)
         on_mutable_global(ct);
      parse_init_expr(code, gv.init, ct);
   }

   void parse_memory_type(wasm_code_ptr& code, memory_type& mt) {
      mt.limits.flags = parse_flags(code);
      mt.limits.initial = parse_varuint32(code);
      // Implementation limits
      detail::check<exceptions::parse>((mt.limits.initial <= detail::get_max_pages(_options)),
                                       "initial memory out of range");
      // WASM specification
      detail::check<exceptions::parse>((mt.limits.initial <= 65536u), "initial memory out of range");
      if (mt.limits.flags) {
         mt.limits.maximum = parse_varuint32(code);
         detail::check<exceptions::parse>((mt.limits.maximum >= mt.limits.initial), "maximum must be at least minimum");
         detail::check<exceptions::parse>((mt.limits.maximum <= 65536u), "maximum memory out of range");
      }
   }

   void parse_export_entry(wasm_code_ptr& code, export_entry& entry) {
      entry.field_str = parse_utf8_string(code, detail::get_max_symbol_bytes(_options));
      entry.kind = (external_kind)(*code++);
      entry.index = parse_varuint32(code);
      switch (entry.kind) {
      case external_kind::Function:
         detail::check<exceptions::parse>((entry.index < _mod->get_functions_total()), "function export out of range");
         break;
      case external_kind::Table:
         detail::check<exceptions::parse>((entry.index < _mod->tables.size()), "table export out of range");
         break;
      case external_kind::Memory:
         detail::check<exceptions::parse>((entry.index < _mod->memories.size()), "memory export out of range");
         break;
      case external_kind::Global:
         detail::check<exceptions::parse>((entry.index < _mod->globals.size()), "global export out of range");
         detail::check<exceptions::parse>(
             (!detail::get_forbid_export_mutable_globals(_options) || !_mod->globals.at(entry.index).type.mutability),
             "cannot export mutable globals");
         break;
      default:
         detail::fail<exceptions::parse>("Unknown export kind");
         break;
      }
   }

   void parse_func_type(wasm_code_ptr& code, func_type& ft) {
      ft.form = *code++;
      detail::check<exceptions::parse>((ft.form == 0x60), "invalid function type");
      decltype(ft.param_types) param_types = {_allocator, parse_varuint32(code)};
      for (size_t i = 0; i < param_types.size(); i++) {
         uint8_t pt = *code++;
         param_types.at(i) = pt;
         detail::check<exceptions::parse>(
             (pt == types::i32 || pt == types::i64 || pt == types::f32 || pt == types::f64),
             "invalid function param type");
      }
      ft.param_types = std::move(param_types);
      ft.return_count = *code++;
      detail::check<exceptions::parse>((ft.return_count < 2), "invalid function return count");
      if (ft.return_count > 0) {
         uint8_t rt = *code++;
         ft.return_type = rt;
         detail::check<exceptions::parse>(
             (rt == types::i32 || rt == types::i64 || rt == types::f32 || rt == types::f64),
             "invalid function return type");
      }
   }

   void parse_elem_segment(wasm_code_ptr& code, elem_segment& es) {
      table_type* tt = nullptr;
      for (std::size_t i = 0; i < _mod->tables.size(); i++) {
         if (_mod->tables[i].element_type == types::anyfunc)
            tt = &(_mod->tables[i]);
      }
      detail::check<exceptions::parse>((tt != nullptr), "table not declared");
      es.index = parse_varuint32(code);
      detail::check<exceptions::parse>((es.index == 0), "only table index of 0 is supported");
      parse_init_expr(code, es.offset, types::i32);
      uint32_t size = parse_varuint32(code);
      detail::check<exceptions::parse>((size <= detail::get_max_element_segment_elements(_options)),
                                       "elem segment too large");
      decltype(es.elems) elems = {_allocator, size};
      for (uint32_t i = 0; i < size; i++) {
         uint32_t index = parse_varuint32(code);
         elems.at(i) = index;
         detail::check<exceptions::parse>((index < _mod->get_functions_total()), "elem for undefined function");
      }
      uint32_t offset = static_cast<uint32_t>(es.offset.value.i32);
      if (static_cast<uint64_t>(size) + offset <= tt->table.size()) {
         std::memcpy(tt->table.raw() + offset, elems.raw(), size * sizeof(uint32_t));
      } else {
         _mod->error = "elem out of range";
      }
      es.elems = std::move(elems);
   }

   void parse_init_expr(wasm_code_ptr& code, init_expr& ie, uint8_t type) {
      ie.opcode = *code++;
      switch (ie.opcode) {
      case opcodes::i32_const:
         ie.value.i32 = parse_varint32(code);
         detail::check<exceptions::parse>((type == types::i32), "expected i32 initializer");
         break;
      case opcodes::i64_const:
         ie.value.i64 = parse_varint64(code);
         detail::check<exceptions::parse>((type == types::i64), "expected i64 initializer");
         break;
      case opcodes::f32_const:
         ie.value.f32 = parse_raw<uint32_t>(code);
         detail::check<exceptions::parse>((type == types::f32), "expected f32 initializer");
         break;
      case opcodes::f64_const:
         ie.value.f64 = parse_raw<uint64_t>(code);
         detail::check<exceptions::parse>((type == types::f64), "expected f64 initializer");
         break;
      default:
         detail::fail<exceptions::parse>(
             "initializer expression can only acception i32.const, i64.const, f32.const and f64.const");
      }
      detail::check<exceptions::parse>(((*code++) == opcodes::end), "no end op found");
   }

   void parse_function_body(wasm_code_ptr& code, function_body& fb, std::size_t idx) {
      fb.size = parse_varuint32(code);
      detail::check<exceptions::parse>((fb.size <= detail::get_max_code_bytes(_options)), "Function body too large");
      const auto& before = code.offset();
      const auto& local_cnt = parse_varuint32(code);
      _current_function_index++;
      detail::check<exceptions::parse>((local_cnt <= detail::get_max_local_sets(_options)),
                                       "Number of local sets exceeds limit");
      decltype(fb.locals) locals = {_allocator, local_cnt};
      func_type& ft = _mod->types.at(_mod->functions.at(idx));
      detail::max_func_local_bytes_checker<Options> local_checker(_options, ft);
      // parse the local entries
      for (size_t i = 0; i < local_cnt; i++) {
         auto count = parse_varuint32(code);
         auto type = *code++;
         if (detail::get_allow_invalid_empty_local_set(_options) && count == 0)
            type = types::i32;
         detail::check<exceptions::parse>(
             (type == types::i32 || type == types::i64 || type == types::f32 || type == types::f64),
             "invalid local type");
         local_checker.on_local(_options, type, count);
         locals.at(i).count = count;
         locals.at(i).type = type;
      }
      fb.locals = std::move(locals);

      fb.size -= code.offset() - before;
      auto guard = code.scoped_shrink_bounds(fb.size);
      _function_bodies.emplace_back(wasm_code_ptr{code.raw(), fb.size}, local_checker);

      code += fb.size - 1;
      detail::check<exceptions::parse>((detail::get_allow_code_after_function_end(_options) || *code == 0x0B),
                                       "failed parsing function body, expected 'end'");
      ++code;
   }

   // The control stack holds either address of the target of the
   // label (for backward jumps) or a list of instructions to be
   // updated (for forward jumps).
   //
   // Inside an if: The first element refers to the `if` and should
   // jump to `else`.  The remaining elements should branch to `end`
   using label_t = decltype(std::declval<Writer>().emit_end());
   using branch_t = decltype(std::declval<Writer>().emit_if());
   struct pc_element_t {
      uint32_t operand_depth;
      uint32_t expected_result;
      uint32_t label_result;
      bool is_if;
      std::variant<label_t, std::vector<branch_t>> relocations;
   };

   static constexpr uint8_t any_type = 0x82;
   struct operand_stack_type_tracker {
      explicit operand_stack_type_tracker(const detail::max_func_local_bytes_stack_checker<Options> local_bytes_checker,
                                          const Options& options)
          : local_bytes_checker(local_bytes_checker), options(options) {}
      std::vector<uint8_t> state = {scope_tag};
      static constexpr uint8_t unreachable_tag = 0x80;
      static constexpr uint8_t scope_tag = 0x81;
      uint32_t operand_depth = 0;
      uint32_t maximum_operand_depth = 0;
      detail::max_func_local_bytes_stack_checker<Options> local_bytes_checker;
      const Options& options;
      void push(uint8_t type) {
         assert(type != unreachable_tag && type != scope_tag);
         assert(type == types::i32 || type == types::i64 || type == types::f32 || type == types::f64 ||
                type == any_type);
         detail::check<exceptions::parse>((operand_depth < std::numeric_limits<uint32_t>::max()),
                                          "integer overflow in operand depth");
         ++operand_depth;
         maximum_operand_depth = std::max(operand_depth, maximum_operand_depth);
         state.push_back(type);
         local_bytes_checker.push_stack(options, type);
      }
      void pop(uint8_t expected) {
         assert(expected != unreachable_tag && expected != scope_tag);
         if (expected == types::pseudo)
            return;
         detail::check<exceptions::parse>((!state.empty()), "unexpected pop");
         if (state.back() != unreachable_tag) {
            detail::check<exceptions::parse>((state.back() == expected || state.back() == any_type), "wrong type");
            local_bytes_checker.pop_stack(options, expected);
            --operand_depth;
            state.pop_back();
         }
      }
      uint8_t pop() {
         detail::check<exceptions::parse>((!state.empty() && state.back() != scope_tag), "unexpected pop");
         if (state.back() == unreachable_tag)
            return any_type;
         else {
            uint8_t result = state.back();
            --operand_depth;
            local_bytes_checker.pop_stack(options, result);
            state.pop_back();
            return result;
         }
      }
      void top(uint8_t expected) {
         // Constrain the top of the stack if it was any_type or unreachable_tag.
         pop(expected);
         push(expected);
      }
      void start_unreachable() {
         while (!state.empty() && state.back() != scope_tag) {
            if (state.back() != unreachable_tag)
               --operand_depth;
            state.pop_back();
         }
         local_bytes_checker.push_unreachable();
         state.push_back(unreachable_tag);
      }
      void push_scope() {
         state.push_back(scope_tag);
      }
      void pop_scope(uint8_t expected_result = types::pseudo) {
         pop(expected_result);
         detail::check<exceptions::parse>((!state.empty()), "unexpected end");
         if (state.back() == unreachable_tag) {
            local_bytes_checker.pop_unreachable();
            state.pop_back();
         }
         detail::check<exceptions::parse>((state.back() == scope_tag), "unexpected end");
         state.pop_back();
         if (expected_result != types::pseudo) {
            push(expected_result);
         }
      }
      void finish() {
         if (!state.empty() && state.back() == unreachable_tag) {
            state.pop_back();
         }
         detail::check<exceptions::parse>((state.empty()), "stack not empty at scope end");
      }
      uint32_t depth() const {
         return operand_depth;
      }
   };

   struct local_types_t {
      local_types_t(const func_type& ft, const guarded_vector<local_entry>& locals_arg) : _ft(ft), _locals(locals_arg) {
         uint32_t count = ft.param_types.size();
         _boundaries.push_back(count);
         for (uint32_t i = 0; i < locals_arg.size(); ++i) {
            // This test cannot overflow.
            detail::check<exceptions::parse>((count <= 0xFFFFFFFFu - locals_arg[i].count), "too many locals");
            count += locals_arg[i].count;
            _boundaries.push_back(count);
         }
      }
      uint8_t operator[](uint32_t local_idx) const {
         detail::check<exceptions::parse>((local_idx < _boundaries.back()), "undefined local");
         auto pos = std::upper_bound(_boundaries.begin(), _boundaries.end(), local_idx);
         if (pos == _boundaries.begin())
            return _ft.param_types[local_idx];
         else
            return _locals[pos - _boundaries.begin() - 1].type;
      }
      uint64_t locals_count() const {
         uint64_t total = _boundaries.back();
         return total - _ft.param_types.size();
      }
      const func_type& _ft;
      const guarded_vector<local_entry>& _locals;
      std::vector<uint32_t> _boundaries;
   };

   void parse_function_body_code(wasm_code_ptr& code, size_t bounds,
                                 const detail::max_func_local_bytes_stack_checker<Options>& local_bytes_checker,
                                 Writer& code_writer, const func_type& ft, const local_types_t& local_types) {

      // Initialize the control stack with the current function as the sole element
      operand_stack_type_tracker op_stack{local_bytes_checker, _options};
      std::vector<pc_element_t> pc_stack{
          {op_stack.depth(), ft.return_count ? ft.return_type : static_cast<uint32_t>(types::pseudo),
           ft.return_count ? ft.return_type : static_cast<uint32_t>(types::pseudo), false, std::vector<branch_t>{}}};

      // writes the continuation of a label to address.  If the continuation
      // is not yet available, address will be recorded in the relocations
      // list for label.
      auto handle_branch_target = [&](uint32_t label, branch_t address) {
         detail::check<exceptions::parse>((label < pc_stack.size()), "invalid label");
         pc_element_t& branch_target = pc_stack[pc_stack.size() - label - 1];
         std::visit(overloaded{[&](label_t target) { code_writer.fix_branch(address, target); },
                               [&](std::vector<branch_t>& relocations) { relocations.push_back(address); }},
                    branch_target.relocations);
      };

      // Returns the number of operands that need to be popped when
      // branching to label.  If the label has a return value it will
      // be counted in this, and the high bit will be set to signal
      // its presence.
      auto compute_depth_change = [&](uint32_t label) -> uint32_t {
         detail::check<exceptions::parse>((label < pc_stack.size()), "invalid label");
         pc_element_t& branch_target = pc_stack[pc_stack.size() - label - 1];
         uint32_t result = op_stack.depth() - branch_target.operand_depth;
         if (branch_target.label_result != types::pseudo) {
            // FIXME: Reusing the high bit imposes an additional constraint
            // on the maximum depth of the operand stack.  This isn't an
            // actual problem right now, because the stack is hard-coded
            // to 8192 elements, but it would be better to avoid spreading
            // this assumption around the code.
            result |= 0x80000000;
            op_stack.top(branch_target.label_result);
         }
         return result;
      };

      // Handles branches to the end of the scope and pops the pc_stack
      auto exit_scope = [&]() {
         // There must be at least one element
         detail::check<exceptions::parse>((pc_stack.size()), "unexpected end instruction");
         // an if with an empty else cannot have a return value
         detail::check<exceptions::parse>((!pc_stack.back().is_if || pc_stack.back().expected_result == types::pseudo),
                                          "wrong type");
         auto end_pos = code_writer.emit_end();
         if (auto* relocations = std::get_if<std::vector<branch_t>>(&pc_stack.back().relocations)) {
            for (auto branch_op : *relocations) {
               code_writer.fix_branch(branch_op, end_pos);
            }
         }
         op_stack.pop_scope(pc_stack.back().expected_result);
         pc_stack.pop_back();
      };

      auto check_in_bounds = [&] {
         detail::check<exceptions::parse>((!detail::get_allow_code_after_function_end(_options) || !pc_stack.empty()),
                                          "code after function end");
      };

      while (code.offset() < bounds) {
         detail::check<exceptions::parse>((pc_stack.size() <= detail::get_max_nested_structures(_options)),
                                          "nested structures validation failure");

         imap.on_instr_start(code_writer.get_addr(), code.raw());

         switch (*code++) {
         case opcodes::unreachable:
            check_in_bounds();
            code_writer.emit_unreachable();
            op_stack.start_unreachable();
            break;
         case opcodes::nop:
            code_writer.emit_nop();
            break;
         case opcodes::end: {
            check_in_bounds();
            exit_scope();
            detail::check<exceptions::parse>(
                (detail::get_allow_code_after_function_end(_options) || !pc_stack.empty() || code.offset() == bounds),
                "function too short");
            _nested_checker.on_end(_options);
            break;
         }
         case opcodes::return_: {
            check_in_bounds();
            uint32_t label = pc_stack.size() - 1;
            auto branch = code_writer.emit_return(compute_depth_change(label));
            handle_branch_target(label, branch);
            op_stack.start_unreachable();
         } break;
         case opcodes::block: {
            uint32_t expected_result = *code++;
            if (detail::get_allow_zero_blocktype(_options) && expected_result == 0)
               expected_result = types::pseudo;
            detail::check<exceptions::parse>((expected_result == types::i32 || expected_result == types::i64 ||
                                              expected_result == types::f32 || expected_result == types::f64 ||
                                              expected_result == types::pseudo),
                                             "Invalid type code in block");
            pc_stack.push_back({op_stack.depth(), expected_result, expected_result, false, std::vector<branch_t>{}});
            code_writer.emit_block();
            op_stack.push_scope();
            _nested_checker.on_control(_options);
         } break;
         case opcodes::loop: {
            uint32_t expected_result = *code++;
            if (detail::get_allow_zero_blocktype(_options) && expected_result == 0)
               expected_result = types::pseudo;
            detail::check<exceptions::parse>((expected_result == types::i32 || expected_result == types::i64 ||
                                              expected_result == types::f32 || expected_result == types::f64 ||
                                              expected_result == types::pseudo),
                                             "Invalid type code in loop");
            auto pos = code_writer.emit_loop();
            pc_stack.push_back({op_stack.depth(), expected_result, types::pseudo, false, pos});
            op_stack.push_scope();
            _nested_checker.on_control(_options);
         } break;
         case opcodes::if_: {
            check_in_bounds();
            uint32_t expected_result = *code++;
            if (detail::get_allow_zero_blocktype(_options) && expected_result == 0)
               expected_result = types::pseudo;
            detail::check<exceptions::parse>((expected_result == types::i32 || expected_result == types::i64 ||
                                              expected_result == types::f32 || expected_result == types::f64 ||
                                              expected_result == types::pseudo),
                                             "Invalid type code in if");
            auto branch = code_writer.emit_if();
            op_stack.pop(types::i32);
            pc_stack.push_back({op_stack.depth(), expected_result, expected_result, true, std::vector{branch}});
            op_stack.push_scope();
            _nested_checker.on_control(_options);
         } break;
         case opcodes::else_: {
            check_in_bounds();
            auto& old_index = pc_stack.back();
            detail::check<exceptions::parse>((old_index.is_if), "else outside if");
            auto& relocations = std::get<std::vector<branch_t>>(old_index.relocations);
            // reset the operand stack to the same state as the if
            op_stack.pop(old_index.expected_result);
            op_stack.pop_scope();
            op_stack.push_scope();
            // Overwrite the branch from the `if` with the `else`.
            // We're left with a normal relocation list where everything
            // branches to the corresponding `end`
            relocations[0] = code_writer.emit_else(relocations[0]);
            old_index.is_if = false;
            _nested_checker.on_control(_options);
            break;
         }
         case opcodes::br: {
            check_in_bounds();
            uint32_t label = parse_varuint32(code);
            auto branch = code_writer.emit_br(compute_depth_change(label));
            handle_branch_target(label, branch);
            op_stack.start_unreachable();
         } break;
         case opcodes::br_if: {
            check_in_bounds();
            uint32_t label = parse_varuint32(code);
            op_stack.pop(types::i32);
            auto branch = code_writer.emit_br_if(compute_depth_change(label));
            handle_branch_target(label, branch);
         } break;
         case opcodes::br_table: {
            check_in_bounds();
            size_t table_size = parse_varuint32(code);
            detail::check<exceptions::parse>((table_size <= detail::get_max_br_table_elements(_options)),
                                             "Too many labels in br_table");
            uint8_t result_type = 0;
            op_stack.pop(types::i32);
            auto handler = code_writer.emit_br_table(table_size);
            for (size_t i = 0; i < table_size; i++) {
               uint32_t label = parse_varuint32(code);
               auto branch = handler.emit_case(compute_depth_change(label));
               handle_branch_target(label, branch);
               uint8_t one_result = pc_stack[pc_stack.size() - label - 1].label_result;
               if (i == 0) {
                  result_type = one_result;
               } else {
                  detail::check<exceptions::parse>((result_type == one_result),
                                                   "br_table labels must have the same type");
               }
            }
            uint32_t label = parse_varuint32(code);
            auto branch = handler.emit_default(compute_depth_change(label));
            handle_branch_target(label, branch);
            detail::check<exceptions::parse>(
                (table_size == 0 || result_type == pc_stack[pc_stack.size() - label - 1].label_result),
                "br_table labels must have the same type");
            op_stack.start_unreachable();
         } break;
         case opcodes::call: {
            check_in_bounds();
            uint32_t funcnum = parse_varuint32(code);
            const func_type& ft = _mod->get_function_type(funcnum);
            for (uint32_t i = 0; i < ft.param_types.size(); ++i)
               op_stack.pop(ft.param_types[ft.param_types.size() - i - 1]);
            detail::check<exceptions::parse>((ft.return_count <= 1), "unsupported");
            if (ft.return_count)
               op_stack.push(ft.return_type);
            code_writer.emit_call(ft, funcnum);
         } break;
         case opcodes::call_indirect: {
            check_in_bounds();
            uint32_t functypeidx = parse_varuint32(code);
            const func_type& ft = _mod->types.at(functypeidx);
            detail::check<exceptions::parse>((_mod->tables.size() > 0), "call_indirect requires a table");
            op_stack.pop(types::i32);
            for (uint32_t i = 0; i < ft.param_types.size(); ++i)
               op_stack.pop(ft.param_types[ft.param_types.size() - i - 1]);
            detail::check<exceptions::parse>((ft.return_count <= 1), "unsupported");
            if (ft.return_count)
               op_stack.push(ft.return_type);
            code_writer.emit_call_indirect(ft, functypeidx);
            detail::check<exceptions::parse>((*code == 0), "call_indirect must end with 0x00.");
            code++; // 0x00
            break;
         }
         case opcodes::drop:
            check_in_bounds();
            code_writer.emit_drop();
            op_stack.pop();
            break;
         case opcodes::select: {
            check_in_bounds();
            code_writer.emit_select();
            op_stack.pop(types::i32);
            uint8_t t0 = op_stack.pop();
            uint8_t t1 = op_stack.pop();
            detail::check<exceptions::parse>((t0 == t1 || t0 == any_type || t1 == any_type),
                                             "incorrect types for select");
            op_stack.push(t0 != any_type ? t0 : t1);
         } break;
         case opcodes::get_local: {
            uint32_t local_idx = parse_varuint32(code);
            op_stack.push(local_types[local_idx]);
            code_writer.emit_get_local(local_idx);
         } break;
         case opcodes::set_local: {
            check_in_bounds();
            uint32_t local_idx = parse_varuint32(code);
            op_stack.pop(local_types[local_idx]);
            code_writer.emit_set_local(local_idx);
         } break;
         case opcodes::tee_local: {
            check_in_bounds();
            uint32_t local_idx = parse_varuint32(code);
            op_stack.top(local_types[local_idx]);
            code_writer.emit_tee_local(local_idx);
         } break;
         case opcodes::get_global: {
            uint32_t global_idx = parse_varuint32(code);
            op_stack.push(_mod->globals.at(global_idx).type.content_type);
            code_writer.emit_get_global(global_idx);
         } break;
         case opcodes::set_global: {
            check_in_bounds();
            uint32_t global_idx = parse_varuint32(code);
            detail::check<exceptions::parse>((_mod->globals.at(global_idx).type.mutability), "cannot set const global");
            op_stack.pop(_mod->globals.at(global_idx).type.content_type);
            code_writer.emit_set_global(global_idx);
         } break;
#define LOAD_OP(op_name, max_align, type)                                                                              \
   case opcodes::op_name: {                                                                                            \
      check_in_bounds();                                                                                               \
      detail::check<exceptions::parse>((_mod->memories.size() > 0), "load requires memory");                           \
      uint32_t alignment = parse_varuint32(code);                                                                      \
      uint32_t offset = parse_varuint32(code);                                                                         \
      detail::check<exceptions::parse>((alignment <= uint32_t(max_align)), "alignment cannot be greater than size.");  \
      detail::check<exceptions::parse>((offset <= detail::get_max_memory_offset(_options)), "load offset too large."); \
      op_stack.pop(types::i32);                                                                                        \
      op_stack.push(types::type);                                                                                      \
      code_writer.emit_##op_name(alignment, offset);                                                                   \
   } break;

            LOAD_OP(i32_load, 2, i32)
            LOAD_OP(i64_load, 3, i64)
            LOAD_OP(f32_load, 2, f32)
            LOAD_OP(f64_load, 3, f64)
            LOAD_OP(i32_load8_s, 0, i32)
            LOAD_OP(i32_load16_s, 1, i32)
            LOAD_OP(i32_load8_u, 0, i32)
            LOAD_OP(i32_load16_u, 1, i32)
            LOAD_OP(i64_load8_s, 0, i64)
            LOAD_OP(i64_load16_s, 1, i64)
            LOAD_OP(i64_load32_s, 2, i64)
            LOAD_OP(i64_load8_u, 0, i64)
            LOAD_OP(i64_load16_u, 1, i64)
            LOAD_OP(i64_load32_u, 2, i64)

#undef LOAD_OP

#define STORE_OP(op_name, max_align, type)                                                                             \
   case opcodes::op_name: {                                                                                            \
      check_in_bounds();                                                                                               \
      detail::check<exceptions::parse>((_mod->memories.size() > 0), "store requires memory");                          \
      uint32_t alignment = parse_varuint32(code);                                                                      \
      uint32_t offset = parse_varuint32(code);                                                                         \
      detail::check<exceptions::parse>((alignment <= uint32_t(max_align)), "alignment cannot be greater than size.");  \
      detail::check<exceptions::parse>((offset <= detail::get_max_memory_offset(_options)),                            \
                                       "store offset too large.");                                                     \
      op_stack.pop(types::type);                                                                                       \
      op_stack.pop(types::i32);                                                                                        \
      code_writer.emit_##op_name(alignment, offset);                                                                   \
   } break;

            STORE_OP(i32_store, 2, i32)
            STORE_OP(i64_store, 3, i64)
            STORE_OP(f32_store, 2, f32)
            STORE_OP(f64_store, 3, f64)
            STORE_OP(i32_store8, 0, i32)
            STORE_OP(i32_store16, 1, i32)
            STORE_OP(i64_store8, 0, i64)
            STORE_OP(i64_store16, 1, i64)
            STORE_OP(i64_store32, 2, i64)

#undef STORE_OP

         case opcodes::current_memory:
            detail::check<exceptions::parse>((_mod->memories.size() != 0), "memory.size requires memory");
            op_stack.push(types::i32);
            detail::check<exceptions::parse>((*code == 0), "memory.size must end with 0x00");
            code++;
            code_writer.emit_current_memory();
            break;
         case opcodes::grow_memory:
            check_in_bounds();
            detail::check<exceptions::parse>((_mod->memories.size() != 0), "memory.grow requires memory");
            op_stack.pop(types::i32);
            op_stack.push(types::i32);
            detail::check<exceptions::parse>((*code == 0), "memory.grow must end with 0x00");
            code++;
            code_writer.emit_grow_memory();
            break;
         case opcodes::i32_const:
            code_writer.emit_i32_const(parse_varint32(code));
            op_stack.push(types::i32);
            break;
         case opcodes::i64_const:
            code_writer.emit_i64_const(parse_varint64(code));
            op_stack.push(types::i64);
            break;
         case opcodes::f32_const: {
            code_writer.emit_f32_const(parse_raw<float>(code));
            op_stack.push(types::f32);
         } break;
         case opcodes::f64_const: {
            code_writer.emit_f64_const(parse_raw<double>(code));
            op_stack.push(types::f64);
         } break;

#define UNOP(opname)                                                                                                   \
   case opcodes::opname:                                                                                               \
      check_in_bounds();                                                                                               \
      code_writer.emit_##opname();                                                                                     \
      op_stack.pop(types::A);                                                                                          \
      op_stack.push(types::R);                                                                                         \
      break;
#define BINOP(opname)                                                                                                  \
   case opcodes::opname:                                                                                               \
      check_in_bounds();                                                                                               \
      code_writer.emit_##opname();                                                                                     \
      op_stack.pop(types::A);                                                                                          \
      op_stack.pop(types::A);                                                                                          \
      op_stack.push(types::R);                                                                                         \
      break;
#define CASTOP(dst, opname, src)                                                                                       \
   case opcodes::dst##_##opname##_##src:                                                                               \
      check_in_bounds();                                                                                               \
      code_writer.emit_##dst##_##opname##_##src();                                                                     \
      op_stack.pop(types::src);                                                                                        \
      op_stack.push(types::dst);                                                                                       \
      break;

#define R i32
#define A i32
            UNOP(i32_eqz)
            BINOP(i32_eq)
            BINOP(i32_ne)
            BINOP(i32_lt_s)
            BINOP(i32_lt_u)
            BINOP(i32_gt_s)
            BINOP(i32_gt_u)
            BINOP(i32_le_s)
            BINOP(i32_le_u)
            BINOP(i32_ge_s)
            BINOP(i32_ge_u)
#undef A
#define A i64
            UNOP(i64_eqz)
            BINOP(i64_eq)
            BINOP(i64_ne)
            BINOP(i64_lt_s)
            BINOP(i64_lt_u)
            BINOP(i64_gt_s)
            BINOP(i64_gt_u)
            BINOP(i64_le_s)
            BINOP(i64_le_u)
            BINOP(i64_ge_s)
            BINOP(i64_ge_u)
#undef A
#define A f32
            BINOP(f32_eq)
            BINOP(f32_ne)
            BINOP(f32_lt)
            BINOP(f32_gt)
            BINOP(f32_le)
            BINOP(f32_ge)
#undef A
#define A f64
            BINOP(f64_eq)
            BINOP(f64_ne)
            BINOP(f64_lt)
            BINOP(f64_gt)
            BINOP(f64_le)
            BINOP(f64_ge)
#undef A
#undef R
#define R A
#define A i32
            UNOP(i32_clz)
            UNOP(i32_ctz)
            UNOP(i32_popcnt)
            BINOP(i32_add)
            BINOP(i32_sub)
            BINOP(i32_mul)
            BINOP(i32_div_s)
            BINOP(i32_div_u)
            BINOP(i32_rem_s)
            BINOP(i32_rem_u)
            BINOP(i32_and)
            BINOP(i32_or)
            BINOP(i32_xor)
            BINOP(i32_shl)
            BINOP(i32_shr_s)
            BINOP(i32_shr_u)
            BINOP(i32_rotl)
            BINOP(i32_rotr)
#undef A
#define A i64
            UNOP(i64_clz)
            UNOP(i64_ctz)
            UNOP(i64_popcnt)
            BINOP(i64_add)
            BINOP(i64_sub)
            BINOP(i64_mul)
            BINOP(i64_div_s)
            BINOP(i64_div_u)
            BINOP(i64_rem_s)
            BINOP(i64_rem_u)
            BINOP(i64_and)
            BINOP(i64_or)
            BINOP(i64_xor)
            BINOP(i64_shl)
            BINOP(i64_shr_s)
            BINOP(i64_shr_u)
            BINOP(i64_rotl)
            BINOP(i64_rotr)
#undef A
#define A f32
            UNOP(f32_abs)
            UNOP(f32_neg)
            UNOP(f32_ceil)
            UNOP(f32_floor)
            UNOP(f32_trunc)
            UNOP(f32_nearest)
            UNOP(f32_sqrt)
            BINOP(f32_add)
            BINOP(f32_sub)
            BINOP(f32_mul)
            BINOP(f32_div)
            BINOP(f32_min)
            BINOP(f32_max)
            BINOP(f32_copysign)
#undef A
#define A f64
            UNOP(f64_abs)
            UNOP(f64_neg)
            UNOP(f64_ceil)
            UNOP(f64_floor)
            UNOP(f64_trunc)
            UNOP(f64_nearest)
            UNOP(f64_sqrt)
            BINOP(f64_add)
            BINOP(f64_sub)
            BINOP(f64_mul)
            BINOP(f64_div)
            BINOP(f64_min)
            BINOP(f64_max)
            BINOP(f64_copysign)
#undef A
#undef R

            CASTOP(i32, wrap, i64)
            CASTOP(i32, trunc_s, f32)
            CASTOP(i32, trunc_u, f32)
            CASTOP(i32, trunc_s, f64)
            CASTOP(i32, trunc_u, f64)
            CASTOP(i64, extend_s, i32)
            CASTOP(i64, extend_u, i32)
            CASTOP(i64, trunc_s, f32)
            CASTOP(i64, trunc_u, f32)
            CASTOP(i64, trunc_s, f64)
            CASTOP(i64, trunc_u, f64)
            CASTOP(f32, convert_s, i32)
            CASTOP(f32, convert_u, i32)
            CASTOP(f32, convert_s, i64)
            CASTOP(f32, convert_u, i64)
            CASTOP(f32, demote, f64)
            CASTOP(f64, convert_s, i32)
            CASTOP(f64, convert_u, i32)
            CASTOP(f64, convert_s, i64)
            CASTOP(f64, convert_u, i64)
            CASTOP(f64, promote, f32)
            CASTOP(i32, reinterpret, f32)
            CASTOP(i64, reinterpret, f64)
            CASTOP(f32, reinterpret, i32)
            CASTOP(f64, reinterpret, i64)

#undef CASTOP
#undef UNOP
#undef BINOP
         default:
            detail::fail<exceptions::parse>("Illegal instruction");
         }
      }
      detail::check<exceptions::parse>((pc_stack.empty()), "function body too long");
      _mod->maximum_stack = std::max(_mod->maximum_stack, static_cast<uint64_t>(op_stack.maximum_operand_depth) +
                                                              local_types.locals_count());
   }

   void parse_data_segment(wasm_code_ptr& code, data_segment& ds) {
      detail::check<exceptions::parse>((_mod->memories.size() != 0), "data requires memory");
      ds.index = parse_varuint32(code);
      parse_init_expr(code, ds.offset, types::i32);
      auto len = parse_varuint32(code);
      detail::check<exceptions::parse>((len <= detail::get_max_data_segment_bytes(_options)),
                                       "data segment too large.");
      detail::check<exceptions::parse>((static_cast<uint64_t>(static_cast<uint32_t>(ds.offset.value.i32)) + len <=
                                        detail::get_max_linear_memory_init(_options)),
                                       "out-of-bounds data section");
      auto guard = code.scoped_shrink_bounds(len);
      ds.data = decltype(ds.data){_allocator, len};
      ds.data.copy(code.raw(), len);
      code += len;
   }

   template <typename Elem, typename ParseFunc>
   inline void parse_section_impl(wasm_code_ptr& code, vec<Elem>& elems, std::uint32_t max_elements,
                                  ParseFunc&& elem_parse) {
      auto count = parse_varuint32(code);
      detail::check<exceptions::parse>((count <= max_elements), "number of section elements exceeded limit");
      elems = vec<Elem>{_allocator, count};
      for (size_t i = 0; i < count; i++) {
         elem_parse(code, elems.at(i), i);
      }
   }

   template <uint8_t id>
   inline void parse_section(wasm_code_ptr& code,
                             vec<typename std::enable_if_t<id == section_id::type_section, func_type>>& elems) {
      parse_section_impl(code, elems, detail::get_max_type_section_elements(_options),
                         [&](wasm_code_ptr& code, func_type& ft, std::size_t /*idx*/) { parse_func_type(code, ft); });
   }
   template <uint8_t id>
   inline void parse_section(wasm_code_ptr& code,
                             vec<typename std::enable_if_t<id == section_id::import_section, import_entry>>& elems) {
      parse_section_impl(
          code, elems, detail::get_max_import_section_elements(_options),
          [&](wasm_code_ptr& code, import_entry& ie, std::size_t /*idx*/) { parse_import_entry(code, ie); });
   }
   template <uint8_t id>
   inline void parse_section(wasm_code_ptr& code,
                             vec<typename std::enable_if_t<id == section_id::function_section, uint32_t>>& elems) {
      parse_section_impl(
          code, elems, detail::get_max_function_section_elements(_options),
          [&](wasm_code_ptr& code, uint32_t& elem, std::size_t /*idx*/) { elem = parse_varuint32(code); });
   }
   template <uint8_t id>
   inline void parse_section(wasm_code_ptr& code,
                             vec<typename std::enable_if_t<id == section_id::table_section, table_type>>& elems) {
      parse_section_impl(code, elems, 1,
                         [&](wasm_code_ptr& code, table_type& tt, std::size_t /*idx*/) { parse_table_type(code, tt); });
   }
   template <uint8_t id>
   inline void parse_section(wasm_code_ptr& code,
                             vec<typename std::enable_if_t<id == section_id::memory_section, memory_type>>& elems) {
      parse_section_impl(code, elems, 1, [&](wasm_code_ptr& code, memory_type& mt, std::size_t idx) {
         detail::check<exceptions::parse>((idx == 0), "only one memory is permitted");
         parse_memory_type(code, mt);
      });
   }
   template <uint8_t id>
   inline void parse_section(wasm_code_ptr& code,
                             vec<typename std::enable_if_t<id == section_id::global_section, global_variable>>& elems) {
      parse_section_impl(
          code, elems, detail::get_max_global_section_elements(_options),
          [&](wasm_code_ptr& code, global_variable& gv, std::size_t /*idx*/) { parse_global_variable(code, gv); });
   }
   template <uint8_t id>
   inline void parse_section(wasm_code_ptr& code,
                             vec<typename std::enable_if_t<id == section_id::export_section, export_entry>>& elems) {
      parse_section_impl(
          code, elems, detail::get_max_export_section_elements(_options),
          [&](wasm_code_ptr& code, export_entry& ee, std::size_t /*idx*/) { parse_export_entry(code, ee); });
   }
   template <uint8_t id>
   inline void parse_section(wasm_code_ptr& code,
                             typename std::enable_if_t<id == section_id::start_section, uint32_t>& start) {
      start = parse_varuint32(code);
      const func_type& ft = _mod->get_function_type(start);
      detail::check<exceptions::parse>((ft.return_count == 0 && ft.param_types.size() == 0), "wrong type for start");
   }
   template <uint8_t id>
   inline void parse_section(wasm_code_ptr& code,
                             vec<typename std::enable_if_t<id == section_id::element_section, elem_segment>>& elems) {
      parse_section_impl(
          code, elems, detail::get_max_element_section_elements(_options),
          [&](wasm_code_ptr& code, elem_segment& es, std::size_t /*idx*/) { parse_elem_segment(code, es); });
   }
   template <uint8_t id>
   inline void parse_section(wasm_code_ptr& code,
                             vec<typename std::enable_if_t<id == section_id::code_section, function_body>>& elems) {
      const void* code_start = code.raw() - code.offset();
      parse_section_impl(
          code, elems, detail::get_max_function_section_elements(_options),
          [&](wasm_code_ptr& code, function_body& fb, std::size_t idx) { parse_function_body(code, fb, idx); });
      detail::check<exceptions::parse>((elems.size() == _mod->functions.size()),
                                       "code section must have the same size as the function section");

      write_code_out(_allocator, code, code_start);
   }

   void write_code_out(growable_allocator& allocator, wasm_code_ptr& code, const void* code_start) {
      Writer code_writer(allocator, code.bounds() - code.offset(), *_mod);
      imap.on_code_start(code_writer.get_base_addr(), code_start);
      for (size_t i = 0; i < _function_bodies.size(); i++) {
         function_body& fb = _mod->code[i];
         func_type& ft = _mod->types.at(_mod->functions.at(i));
         local_types_t local_types(ft, fb.locals);
         imap.on_function_start(code_writer.get_addr(), _function_bodies[i].first.raw());
         code_writer.emit_prologue(ft, fb.locals, i);
         parse_function_body_code(_function_bodies[i].first, fb.size, _function_bodies[i].second, code_writer, ft,
                                  local_types);
         code_writer.emit_epilogue(ft, fb.locals, i);
         code_writer.finalize(fb);
      }
      imap.on_code_end(code_writer.get_addr(), code.raw());
   }

   template <uint8_t id>
   inline void parse_section(wasm_code_ptr& code,
                             vec<typename std::enable_if_t<id == section_id::data_section, data_segment>>& elems) {
      parse_section_impl(
          code, elems, detail::get_max_data_section_elements(_options),
          [&](wasm_code_ptr& code, data_segment& ds, std::size_t /*idx*/) { parse_data_segment(code, ds); });
   }

   template <size_t N> varint<N> parse_varint(const wasm_code& code, size_t index) {
      varint<N> result(0);
      result.set(code, index);
      return result;
   }

   template <size_t N> varuint<N> parse_varuint(const wasm_code& code, size_t index) {
      varuint<N> result(0);
      result.set(code, index);
      return result;
   }

   void on_mutable_global(uint8_t type) {
      _globals_checker.on_mutable_global(_options, type);
   }

   void validate_exports() const {
      std::vector<const guarded_vector<uint8_t>*> export_names;
      export_names.reserve(_mod->exports.size());
      for (uint32_t i = 0; i < _mod->exports.size(); ++i) {
         export_names.push_back(&_mod->exports[i].field_str);
      }
      std::sort(export_names.begin(), export_names.end(), [](auto* lhs, auto* rhs) {
         return std::lexicographical_compare(lhs->raw(), lhs->raw() + lhs->size(), rhs->raw(),
                                             rhs->raw() + rhs->size());
      });
      auto it = std::adjacent_find(export_names.begin(), export_names.end(), [](auto* lhs, auto* rhs) {
         return lhs->size() == rhs->size() && std::equal(lhs->raw(), lhs->raw() + lhs->size(), rhs->raw());
      });
      detail::check<exceptions::parse>((it == export_names.end()), "duplicate export name");
   }

 private:
   growable_allocator& _allocator;
   Options _options;
   module* _mod; // non-owning weak pointer
   int64_t _current_function_index = -1;
   uint64_t _maximum_function_stack_usage = 0; // non-parameter locals + stack
   std::vector<std::pair<wasm_code_ptr, detail::max_func_local_bytes_stack_checker<Options>>> _function_bodies;
   detail::max_mutable_globals_checker<Options> _globals_checker;
   detail::max_control_depth_checker<Options> _nested_checker;
   typename DebugInfo::builder imap;
};
} // namespace forge::vm::wasm

#if defined(__x86_64__)
namespace forge::vm::wasm {

// Random notes:
// - branch instructions return the address that will need to be updated
// - label instructions return the address of the target
// - fix_branch will be called when the branch target is resolved
// - It would make everything more efficient to make RAX always represent the top of
//   the stack.
//
// - The base of memory is stored in rsi
//
// - FIXME: Factor the machine instructions into a separate assembler class.
template <typename Context> class machine_code_writer {
 public:
   machine_code_writer(growable_allocator& alloc, std::size_t source_bytes, module& mod)
       : _mod(mod), _allocator(alloc), _code_segment_base(_allocator.start_code()) {
      const std::size_t code_size = 4 * 16; // 4 error handlers, each is 16 bytes.
      _code_start = _allocator.alloc<unsigned char>(code_size);
      _code_end = _code_start + code_size;
      code = _code_start;

      // always emit these functions
      fpe_handler = emit_error_handler(&on_fp_error);
      call_indirect_handler = emit_error_handler(&on_call_indirect_error);
      type_error_handler = emit_error_handler(&on_type_error);
      stack_overflow_handler = emit_error_handler(&on_stack_overflow);

      assert(code == _code_end); // verify that the manual instruction count is correct

      // emit host functions
      const uint32_t num_imported = mod.get_imported_functions_size();
      const std::size_t host_functions_size = (40 + 10 * Context::async_backtrace()) * num_imported;
      _code_start = _allocator.alloc<unsigned char>(host_functions_size);
      _code_end = _code_start + host_functions_size;
      // code already set
      for (uint32_t i = 0; i < num_imported; ++i) {
         start_function(code, i);
         emit_host_call(i);
      }
      assert(code == _code_end);

      jmp_table = code;
      if (_mod.tables.size() > 0) {
         // Each function table entry consumes exactly 17 bytes (counted
         // manually).  The size must be constant, so that call_indirect
         // can use random access
         _table_element_size = 17;
         const std::size_t table_size = _table_element_size * _mod.tables[0].table.size();
         _code_start = _allocator.alloc<unsigned char>(table_size);
         _code_end = _code_start + table_size;
         // code already set
         for (uint32_t i = 0; i < _mod.tables[0].table.size(); ++i) {
            uint32_t fn_idx = _mod.tables[0].table[i];
            if (fn_idx < _mod.fast_functions.size()) {
               // cmp _mod.fast_functions[fn_idx], %edx
               emit_bytes(0x81, 0xfa);
               emit_operand32(_mod.fast_functions[fn_idx]);
               // je fn
               emit_bytes(0x0f, 0x84);
               register_call(emit_branch_target32(), fn_idx);
               // jmp ERROR
               emit_bytes(0xe9);
               fix_branch(emit_branch_target32(), type_error_handler);
            } else {
               // jmp ERROR
               emit_bytes(0xe9);
               // default for out-of-range functions
               fix_branch(emit_branch_target32(), call_indirect_handler);
               // padding
               emit_bytes(0xcc, 0xcc, 0xcc, 0xcc);
               emit_bytes(0xcc, 0xcc, 0xcc, 0xcc);
               emit_bytes(0xcc, 0xcc, 0xcc, 0xcc);
            }
         }
         assert(code == _code_end);
      }
   }
   ~machine_code_writer() {
      _allocator.end_code<true>(_code_segment_base);
   }

   static constexpr std::size_t max_prologue_size = 21;
   static constexpr std::size_t max_epilogue_size = 10;
   void emit_prologue(const func_type& /*ft*/, const guarded_vector<local_entry>& locals, uint32_t funcnum) {
      _ft = &_mod.types[_mod.functions[funcnum]];
      // FIXME: This is not a tight upper bound
      const std::size_t instruction_size_ratio_upper_bound =
          use_softfloat ? (Context::async_backtrace() ? 63 : 49) : 79;
      std::size_t code_size =
          max_prologue_size + _mod.code[funcnum].size * instruction_size_ratio_upper_bound + max_epilogue_size;
      _code_start = _allocator.alloc<unsigned char>(code_size);
      _code_end = _code_start + code_size;
      code = _code_start;
      start_function(code, funcnum + _mod.get_imported_functions_size());
      // pushq RBP
      emit_bytes(0x55);
      // movq RSP, RBP
      emit_bytes(0x48, 0x89, 0xe5);
      // No more than 2^32-1 locals.  Already validated by the parser.
      uint32_t count = 0;
      for (uint32_t i = 0; i < locals.size(); ++i) {
         assert(uint64_t(count) + locals[i].count <= 0xFFFFFFFFu);
         count += locals[i].count;
      }
      _local_count = count;
      if (_local_count > 0) {
         // xor %rax, %rax
         emit_bytes(0x48, 0x31, 0xc0);
         if (_local_count > 14) { // only use a loop if it would save space
            // mov $count, %ecx
            emit_bytes(0xb9);
            emit_operand32(_local_count);
            // loop:
            void* loop = code;
            // pushq %rax
            emit_bytes(0x50);
            // dec %ecx
            emit_bytes(0xff, 0xc9);
            // jnz loop
            emit_bytes(0x0f, 0x85);
            fix_branch(emit_branch_target32(), loop);
         } else {
            for (uint32_t i = 0; i < _local_count; ++i) {
               // pushq %rax
               emit_bytes(0x50);
            }
         }
      }
      assert((char*)code <= (char*)_code_start + max_prologue_size);
   }
   void emit_epilogue(const func_type& ft, const guarded_vector<local_entry>& locals, uint32_t /*funcnum*/) {
#ifndef NDEBUG
      void* epilogue_start = code;
#endif
      if (ft.return_count != 0) {
         // pop RAX
         emit_bytes(0x58);
      }
      if (_local_count & 0xF0000000u)
         unimplemented();
      emit_multipop(_local_count);
      // popq RBP
      emit_bytes(0x5d);
      // retq
      emit_bytes(0xc3);
      assert((char*)code <= (char*)epilogue_start + max_epilogue_size);
   }

   void emit_unreachable() {
      auto icount = fixed_size_instr(16);
      emit_error_handler(&on_unreachable);
   }
   void emit_nop() {}
   void* emit_end() {
      return code;
   }
   void* emit_return(uint32_t depth_change) {
      // Return is defined as equivalent to branching to the outermost label
      return emit_br(depth_change);
   }
   void emit_block() {}
   void* emit_loop() {
      return code;
   }
   void* emit_if() {
      auto icount = fixed_size_instr(9);
      // pop RAX
      emit_bytes(0x58);
      // test EAX, EAX
      emit_bytes(0x85, 0xC0);
      // jz DEST
      emit_bytes(0x0F, 0x84);
      return emit_branch_target32();
   }
   void* emit_else(void* if_loc) {
      auto icount = fixed_size_instr(5);
      void* result = emit_br(0);
      fix_branch(if_loc, code);
      return result;
   }
   void* emit_br(uint32_t depth_change) {
      auto icount = variable_size_instr(5, 17);
      // add RSP, depth_change * 8
      emit_multipop(depth_change);
      // jmp DEST
      emit_bytes(0xe9);
      return emit_branch_target32();
   }
   void* emit_br_if(uint32_t depth_change) {
      auto icount = variable_size_instr(9, 26);
      // pop RAX
      emit_bytes(0x58);
      // test EAX, EAX
      emit_bytes(0x85, 0xC0);

      if (depth_change == 0u || depth_change == 0x80000001u) {
         // jnz DEST
         emit_bytes(0x0F, 0x85);
         return emit_branch_target32();
      } else {
         // jz SKIP
         emit_bytes(0x0f, 0x84);
         void* skip = emit_branch_target32();
         // add depth_change*8, %rsp
         emit_multipop(depth_change);
         // jmp DEST
         emit_bytes(0xe9);
         void* result = emit_branch_target32();
         // SKIP:
         fix_branch(skip, code);
         return result;
      }
   }

   // Generate a binary search.
   struct br_table_generator {
      void* emit_case(uint32_t depth_change) {
         while (true) {
            assert(!stack.empty() && "The parser is supposed to handle the number of elements in br_table.");
            auto [min, max, label] = stack.back();
            stack.pop_back();
            if (label) {
               fix_branch(label, _this->code);
            }
            if (max - min > 1) {
               // Emit a comparison to the midpoint of the current range
               uint32_t mid = min + (max - min) / 2;
               // cmp i, %mid
               _this->emit_bytes(0x3d);
               _this->emit_operand32(mid);
               // jae MID
               _this->emit_bytes(0x0f, 0x83);
               void* mid_label = _this->emit_branch_target32();
               stack.push_back({mid, max, mid_label});
               stack.push_back({min, mid, nullptr});
            } else {
               assert(min == static_cast<uint32_t>(_i));
               _i++;
               if (depth_change == 0u || depth_change == 0x80000001u) {
                  if (label) {
                     return label;
                  } else {
                     // jmp TARGET
                     _this->emit_bytes(0xe9);
                     return _this->emit_branch_target32();
                  }
               } else {
                  // jne NEXT
                  _this->emit_multipop(depth_change);
                  // jmp TARGET
                  _this->emit_bytes(0xe9);
                  return _this->emit_branch_target32();
               }
            }
         }
      }
      void* emit_default(uint32_t depth_change) {
         void* result = emit_case(depth_change);
         assert(stack.empty() && "unexpected default.");
         return result;
      }
      machine_code_writer* _this;
      int _i = 0;
      struct stack_item {
         uint32_t min;
         uint32_t max;
         void* branch_target = nullptr;
      };
      // stores a stack of ranges to be handled.
      // the ranges are strictly contiguous and non-ovelapping, with
      // the lower values at the back.
      std::vector<stack_item> stack;
   };
   br_table_generator emit_br_table(uint32_t table_size) {
      // pop %rax
      emit_bytes(0x58);
      // Increase the size by one to account for the default.
      // The current algorithm handles this correctly, without
      // any special cases.
      return {this, 0, {{0, table_size + 1, nullptr}}};
   }

   void register_call(void* ptr, uint32_t funcnum) {
      auto& vec = _function_relocations;
      if (funcnum >= vec.size())
         vec.resize(funcnum + 1);
      if (void** addr = std::get_if<void*>(&vec[funcnum])) {
         fix_branch(ptr, *addr);
      } else {
         std::get<std::vector<void*>>(vec[funcnum]).push_back(ptr);
      }
   }
   void start_function(void* func_start, uint32_t funcnum) {
      auto& vec = _function_relocations;
      if (funcnum >= vec.size())
         vec.resize(funcnum + 1);
      for (void* branch : std::get<std::vector<void*>>(vec[funcnum])) {
         fix_branch(branch, func_start);
      }
      vec[funcnum] = func_start;
   }

   void emit_call(const func_type& ft, uint32_t funcnum) {
      auto icount = variable_size_instr(15, 23);
      emit_check_call_depth();
      // callq TARGET
      emit_bytes(0xe8);
      void* branch = emit_branch_target32();
      emit_multipop(ft.param_types.size());
      register_call(branch, funcnum);
      if (ft.return_count != 0)
         // pushq %rax
         emit_bytes(0x50);
      emit_check_call_depth_end();
   }

   void emit_call_indirect(const func_type& ft, uint32_t functypeidx) {
      auto icount = variable_size_instr(43, 51);
      emit_check_call_depth();
      auto& table = _mod.tables[0].table;
      functypeidx = _mod.type_aliases[functypeidx];
      // pop %rax
      emit_bytes(0x58);
      // cmp $size, %rax
      emit_bytes(0x48, 0x3d);
      emit_operand32(table.size());
      // jae ERROR
      emit_bytes(0x0f, 0x83);
      fix_branch(emit_branch_target32(), call_indirect_handler);
      // leaq table(%rip), %rdx
      emit_bytes(0x48, 0x8d, 0x15);
      fix_branch(emit_branch_target32(), jmp_table);
      // imul $17, %eax, %eax
      assert(_table_element_size <= 127); // must fit in 8-bit signed value for imul
      emit_bytes(0x6b, 0xc0, _table_element_size);
      // addq %rdx, %rax
      emit_bytes(0x48, 0x01, 0xd0);
      // mov $funtypeidx, %edx
      emit_bytes(0xba);
      emit_operand32(functypeidx);
      // callq *%rax
      emit_bytes(0xff, 0xd0);
      emit_multipop(ft.param_types.size());
      if (ft.return_count != 0)
         // pushq %rax
         emit_bytes(0x50);
      emit_check_call_depth_end();
   }

   void emit_drop() {
      // pop RAX
      emit_bytes(0x58);
   }

   void emit_select() {
      auto icount = fixed_size_instr(13);
      // popq RAX
      emit_bytes(0x58);
      // popq RCX
      emit_bytes(0x59);
      // test EAX, EAX
      emit_bytes(0x85, 0xc0);
      // cmovnzq RCX, (RSP)
      emit_bytes(0x48, 0x0f, 0x45, 0x0c, 0x24);
      // movq (RSP), RCX
      emit_bytes(0x48, 0x89, 0x0c, 0x24);
   }

   void emit_get_local(uint32_t local_idx) {
      auto icount = fixed_size_instr(8);
      // stack layout:
      //   param0    <----- %rbp + 8*(nparams + 1)
      //   param1
      //   param2
      //   ...
      //   paramN
      //   return address
      //   old %rbp    <------ %rbp
      //   local0    <------ %rbp - 8
      //   local1
      //   ...
      //   localN
      if (local_idx < _ft->param_types.size()) {
         // mov 8*(local_idx)(%RBP), RAX
         emit_bytes(0x48, 0x8b, 0x85);
         emit_operand32(8 * (_ft->param_types.size() - local_idx + 1));
         // push RAX
         emit_bytes(0x50);
      } else {
         // mov -8*(local_idx+1)(%RBP), RAX
         emit_bytes(0x48, 0x8b, 0x85);
         emit_operand32(-8 * (local_idx - _ft->param_types.size() + 1));
         // push RAX
         emit_bytes(0x50);
      }
   }

   void emit_set_local(uint32_t local_idx) {
      auto icount = fixed_size_instr(8);
      if (local_idx < _ft->param_types.size()) {
         // pop RAX
         emit_bytes(0x58);
         // mov RAX, -8*local_idx(EBP)
         emit_bytes(0x48, 0x89, 0x85);
         emit_operand32(8 * (_ft->param_types.size() - local_idx + 1));
      } else {
         // pop RAX
         emit_bytes(0x58);
         // mov RAX, -8*local_idx(EBP)
         emit_bytes(0x48, 0x89, 0x85);
         emit_operand32(-8 * (local_idx - _ft->param_types.size() + 1));
      }
   }

   void emit_tee_local(uint32_t local_idx) {
      auto icount = fixed_size_instr(9);
      if (local_idx < _ft->param_types.size()) {
         // pop RAX
         emit_bytes(0x58);
         // push RAX
         emit_bytes(0x50);
         // mov RAX, -8*local_idx(EBP)
         emit_bytes(0x48, 0x89, 0x85);
         emit_operand32(8 * (_ft->param_types.size() - local_idx + 1));
      } else {
         // pop RAX
         emit_bytes(0x58);
         // push RAX
         emit_bytes(0x50);
         // mov RAX, -8*local_idx(EBP)
         emit_bytes(0x48, 0x89, 0x85);
         emit_operand32(-8 * (local_idx - _ft->param_types.size() + 1));
      }
   }

   void emit_get_global(uint32_t globalidx) {
      auto icount = variable_size_instr(
          24, 42); // emit_setup_backtrace can be 0 or 9, and emit_restore_backtrace 0 or 9, the total of the rest 24
      auto& gl = _mod.globals[globalidx];
      emit_setup_backtrace();
      // pushq %rdi -- save %rdi content onto stack
      emit_bytes(0x57);
      // pushq %rsi -- save %rsi content onto stack
      emit_bytes(0x56);
      // movq $globalidx, %rsi -- pass globalidx to %rsi, the second argument
      emit_bytes(0x48, 0xc7, 0xc6);
      emit_operand32(globalidx);
      // movabsq $get_global, %rax
      emit_bytes(0x48, 0xb8);
      switch (gl.type.content_type) {
      case types::i32:
         emit_operand_ptr(&get_global_i32);
         break;
      case types::i64:
         emit_operand_ptr(&get_global_i64);
         break;
      case types::f32:
         emit_operand_ptr(&get_global_f32);
         break;
      case types::f64:
         emit_operand_ptr(&get_global_f64);
         break;
      }
      // call *%rax
      emit_bytes(0xff, 0xd0);
      // pop %rsi
      emit_bytes(0x5e);
      // pop %rdi
      emit_bytes(0x5f);
      emit_restore_backtrace();
      // push %rax -- return result
      emit_bytes(0x50);
   }

   void emit_set_global(uint32_t globalidx) {
      auto icount = variable_size_instr(
          24, 42); // emit_setup_backtrace can be 0 or 9, and emit_restore_backtrace 0 or 9, the total of the rest 24
      auto& gl = _mod.globals[globalidx];
      // popq %rdx -- pass global value to %rdx, the third argument in set_global
      emit_bytes(0x5a);
      emit_setup_backtrace();
      // pushq %rdi -- save %rdi content onto stack
      emit_bytes(0x57);
      // pushq %rsi -- save %rsi content onto stack
      emit_bytes(0x56);
      // movq $globalidx, %rsi -- pass globalidx to %rsi, the second argument
      emit_bytes(0x48, 0xc7, 0xc6);
      emit_operand32(globalidx);
      // movabsq $set_global, %rax
      emit_bytes(0x48, 0xb8);
      // emit_operand_ptr(&set_global);
      switch (gl.type.content_type) {
      case types::i32:
         emit_operand_ptr(&set_global_i32);
         break;
      case types::i64:
         emit_operand_ptr(&set_global_i64);
         break;
      case types::f32:
         emit_operand_ptr(&set_global_f32);
         break;
      case types::f64:
         emit_operand_ptr(&set_global_f64);
         break;
      }
      // call *%rax
      emit_bytes(0xff, 0xd0);
      // pop %rsi
      emit_bytes(0x5e);
      // pop %rdi
      emit_bytes(0x5f);
      emit_restore_backtrace();
   }

   void emit_i32_load(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(7, 15);
      // movl (RAX), EAX
      emit_load_impl(offset, 0x8b, 0x00);
   }

   void emit_i64_load(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movq (RAX), RAX
      emit_load_impl(offset, 0x48, 0x8b, 0x00);
   }

   void emit_f32_load(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(7, 15);
      // movl (RAX), EAX
      emit_load_impl(offset, 0x8b, 0x00);
   }

   void emit_f64_load(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movq (RAX), RAX
      emit_load_impl(offset, 0x48, 0x8b, 0x00);
   }

   void emit_i32_load8_s(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movsbl (RAX), EAX;
      emit_load_impl(offset, 0x0F, 0xbe, 0x00);
   }

   void emit_i32_load16_s(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movswl (RAX), EAX;
      emit_load_impl(offset, 0x0F, 0xbf, 0x00);
   }

   void emit_i32_load8_u(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movzbl (RAX), EAX;
      emit_load_impl(offset, 0x0f, 0xb6, 0x00);
   }

   void emit_i32_load16_u(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movzwl (RAX), EAX;
      emit_load_impl(offset, 0x0f, 0xb7, 0x00);
   }

   void emit_i64_load8_s(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(9, 17);
      // movsbq (RAX), RAX;
      emit_load_impl(offset, 0x48, 0x0F, 0xbe, 0x00);
   }

   void emit_i64_load16_s(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(9, 17);
      // movswq (RAX), RAX;
      emit_load_impl(offset, 0x48, 0x0F, 0xbf, 0x00);
   }

   void emit_i64_load32_s(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movslq (RAX), RAX
      emit_load_impl(offset, 0x48, 0x63, 0x00);
   }

   void emit_i64_load8_u(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movzbl (RAX), EAX;
      emit_load_impl(offset, 0x0f, 0xb6, 0x00);
   }

   void emit_i64_load16_u(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movzwl (RAX), EAX;
      emit_load_impl(offset, 0x0f, 0xb7, 0x00);
   }

   void emit_i64_load32_u(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(7, 15);
      // movl (RAX), EAX
      emit_load_impl(offset, 0x8b, 0x00);
   }

   void emit_i32_store(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(7, 15);
      // movl ECX, (RAX)
      emit_store_impl(offset, 0x89, 0x08);
   }

   void emit_i64_store(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movl ECX, (RAX)
      emit_store_impl(offset, 0x48, 0x89, 0x08);
   }

   void emit_f32_store(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(7, 15);
      // movl ECX, (RAX)
      emit_store_impl(offset, 0x89, 0x08);
   }

   void emit_f64_store(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movl ECX, (RAX)
      emit_store_impl(offset, 0x48, 0x89, 0x08);
   }

   void emit_i32_store8(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(7, 15);
      // movb CL, (RAX)
      emit_store_impl(offset, 0x88, 0x08);
   }

   void emit_i32_store16(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movb CX, (RAX)
      emit_store_impl(offset, 0x66, 0x89, 0x08);
   }

   void emit_i64_store8(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(7, 15);
      // movb CL, (RAX)
      emit_store_impl(offset, 0x88, 0x08);
   }

   void emit_i64_store16(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(8, 16);
      // movb CX, (RAX)
      emit_store_impl(offset, 0x66, 0x89, 0x08);
   }

   void emit_i64_store32(uint32_t /*alignment*/, uint32_t offset) {
      auto icount = variable_size_instr(7, 15);
      // movl ECX, (RAX)
      emit_store_impl(offset, 0x89, 0x08);
   }

   void emit_current_memory() {
      auto icount = variable_size_instr(17, 35);
      emit_setup_backtrace();
      // pushq %rdi
      emit_bytes(0x57);
      // pushq %rsi
      emit_bytes(0x56);
      // movabsq $current_memory, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand_ptr(&current_memory);
      // call *%rax
      emit_bytes(0xff, 0xd0);
      // pop %rsi
      emit_bytes(0x5e);
      // pop %rdi
      emit_bytes(0x5f);
      emit_restore_backtrace();
      // push %rax
      emit_bytes(0x50);
   }
   void emit_grow_memory() {
      auto icount = variable_size_instr(21, 39);
      // popq %rax
      emit_bytes(0x58);
      emit_setup_backtrace();
      // pushq %rdi
      emit_bytes(0x57);
      // pushq %rsi
      emit_bytes(0x56);
      // movq %rax, %rsi
      emit_bytes(0x48, 0x89, 0xc6);
      // movabsq $grow_memory, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand_ptr(&grow_memory);
      // call *%rax
      emit_bytes(0xff, 0xd0);
      // pop %rsi
      emit_bytes(0x5e);
      // pop %rdi
      emit_bytes(0x5f);
      emit_restore_backtrace();
      // push %rax
      emit_bytes(0x50);
   }

   void emit_i32_const(uint32_t value) {
      auto icount = fixed_size_instr(6);
      // mov $value, %eax
      emit_bytes(0xb8);
      emit_operand32(value);
      // push %rax
      emit_bytes(0x50);
   }

   void emit_i64_const(uint64_t value) {
      auto icount = fixed_size_instr(11);
      // movabsq $value, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand64(value);
      // push %rax
      emit_bytes(0x50);
   }

   void emit_f32_const(float value) {
      auto icount = fixed_size_instr(6);
      // mov $value, %eax
      emit_bytes(0xb8);
      emit_operandf32(value);
      // push %rax
      emit_bytes(0x50);
   }
   void emit_f64_const(double value) {
      auto icount = fixed_size_instr(11);
      // movabsq $value, %rax
      emit_bytes(0x48, 0xb8);
      emit_operandf64(value);
      // push %rax
      emit_bytes(0x50);
   }

   void emit_i32_eqz() {
      auto icount = fixed_size_instr(10);
      // pop %rax
      emit_bytes(0x58);
      // xor %rcx, %rcx
      emit_bytes(0x48, 0x31, 0xc9);
      // test %eax, %eax
      emit_bytes(0x85, 0xc0);
      // setz %cl
      emit_bytes(0x0f, 0x94, 0xc1);
      // push %rcx
      emit_bytes(0x51);
   }

   // i32 relops
   void emit_i32_eq() {
      auto icount = fixed_size_instr(11);
      // sete %dl
      emit_i32_relop(0x94);
   }

   void emit_i32_ne() {
      auto icount = fixed_size_instr(11);
      // sete %dl
      emit_i32_relop(0x95);
   }

   void emit_i32_lt_s() {
      auto icount = fixed_size_instr(11);
      // setl %dl
      emit_i32_relop(0x9c);
   }

   void emit_i32_lt_u() {
      auto icount = fixed_size_instr(11);
      // setl %dl
      emit_i32_relop(0x92);
   }

   void emit_i32_gt_s() {
      auto icount = fixed_size_instr(11);
      // setg %dl
      emit_i32_relop(0x9f);
   }

   void emit_i32_gt_u() {
      auto icount = fixed_size_instr(11);
      // seta %dl
      emit_i32_relop(0x97);
   }

   void emit_i32_le_s() {
      auto icount = fixed_size_instr(11);
      // setle %dl
      emit_i32_relop(0x9e);
   }

   void emit_i32_le_u() {
      auto icount = fixed_size_instr(11);
      // setbe %dl
      emit_i32_relop(0x96);
   }

   void emit_i32_ge_s() {
      auto icount = fixed_size_instr(11);
      // setge %dl
      emit_i32_relop(0x9d);
   }

   void emit_i32_ge_u() {
      auto icount = fixed_size_instr(11);
      // setae %dl
      emit_i32_relop(0x93);
   }

   void emit_i64_eqz() {
      auto icount = fixed_size_instr(11);
      // pop %rax
      emit_bytes(0x58);
      // xor %rcx, %rcx
      emit_bytes(0x48, 0x31, 0xc9);
      // test %rax, %rax
      emit_bytes(0x48, 0x85, 0xc0);
      // setz %cl
      emit_bytes(0x0f, 0x94, 0xc1);
      // push %rcx
      emit_bytes(0x51);
   }
   // i64 relops
   void emit_i64_eq() {
      auto icount = fixed_size_instr(12);
      // sete %dl
      emit_i64_relop(0x94);
   }

   void emit_i64_ne() {
      auto icount = fixed_size_instr(12);
      // sete %dl
      emit_i64_relop(0x95);
   }

   void emit_i64_lt_s() {
      auto icount = fixed_size_instr(12);
      // setl %dl
      emit_i64_relop(0x9c);
   }

   void emit_i64_lt_u() {
      auto icount = fixed_size_instr(12);
      // setl %dl
      emit_i64_relop(0x92);
   }

   void emit_i64_gt_s() {
      auto icount = fixed_size_instr(12);
      // setg %dl
      emit_i64_relop(0x9f);
   }

   void emit_i64_gt_u() {
      auto icount = fixed_size_instr(12);
      // seta %dl
      emit_i64_relop(0x97);
   }

   void emit_i64_le_s() {
      auto icount = fixed_size_instr(12);
      // setle %dl
      emit_i64_relop(0x9e);
   }

   void emit_i64_le_u() {
      auto icount = fixed_size_instr(12);
      // setbe %dl
      emit_i64_relop(0x96);
   }

   void emit_i64_ge_s() {
      auto icount = fixed_size_instr(12);
      // setge %dl
      emit_i64_relop(0x9d);
   }

   void emit_i64_ge_u() {
      auto icount = fixed_size_instr(12);
      // setae %dl
      emit_i64_relop(0x93);
   }

   // Make sure that the result doesn't contain any garbage bits in rax
   static uint64_t adapt_result(bool val) {
      return val ? 1 : 0;
   }
   static uint64_t adapt_result(float32_t val) {
      uint64_t result = 0;
      std::memcpy(&result, &val, sizeof(float32_t));
      return result;
   }
   static float64_t adapt_result(float64_t val) {
      return val;
   }

   template <auto F> static auto adapt_f32_unop(float32_t arg) {
      return ::to_softfloat32(static_cast<decltype(F)>(F)(::from_softfloat32(arg)));
   }
   template <auto F> static auto adapt_f32_binop(float32_t lhs, float32_t rhs) {
      return ::to_softfloat32(static_cast<decltype(F)>(F)(::from_softfloat32(lhs), ::from_softfloat32(rhs)));
   }
   template <auto F> static auto adapt_f32_cmp(float32_t lhs, float32_t rhs) {
      return adapt_result(static_cast<decltype(F)>(F)(::from_softfloat32(lhs), ::from_softfloat32(rhs)));
   }

   template <auto F> static auto adapt_f64_unop(float64_t arg) {
      return ::to_softfloat64(static_cast<decltype(F)>(F)(::from_softfloat64(arg)));
   }
   template <auto F> static auto adapt_f64_binop(float64_t lhs, float64_t rhs) {
      return ::to_softfloat64(static_cast<decltype(F)>(F)(::from_softfloat64(lhs), ::from_softfloat64(rhs)));
   }
   template <auto F> static auto adapt_f64_cmp(float64_t lhs, float64_t rhs) {
      return adapt_result(static_cast<decltype(F)>(F)(::from_softfloat64(lhs), ::from_softfloat64(rhs)));
   }

   static float32_t to_softfloat(float arg) {
      return ::to_softfloat32(arg);
   }
   static float64_t to_softfloat(double arg) {
      return ::to_softfloat64(arg);
   }
   template <typename T> static T to_softfloat(T arg) {
      return arg;
   }
   static float from_softfloat(float32_t arg) {
      return ::from_softfloat32(arg);
   }
   static double from_softfloat(float64_t arg) {
      return ::from_softfloat64(arg);
   }
   template <typename T> static T from_softfloat(T arg) {
      return arg;
   }

   template <typename T> using softfloat_arg_t = decltype(to_softfloat(T{}));

   template <auto F, typename T> static auto adapt_float_convert(softfloat_arg_t<T> arg) {
      auto result = to_softfloat(F(from_softfloat(arg)));
      if constexpr (sizeof(result) == 4 && sizeof(T) == 8) {
         uint64_t buffer = 0;
         std::memcpy(&buffer, &result, sizeof(result));
         return buffer;
      } else {
         return result;
      }
   }

   template <auto F, typename R, typename T> static constexpr auto choose_unop(R (*)(T)) {
      if constexpr (sizeof(R) == 4 && sizeof(T) == 8) {
         return static_cast<uint64_t (*)(softfloat_arg_t<T>)>(&adapt_float_convert<F, T>);
      } else {
         return static_cast<softfloat_arg_t<R> (*)(softfloat_arg_t<T>)>(&adapt_float_convert<F, T>);
      }
   }

   // HACK: avoid linking to softfloat if we aren't using it
   // and also avoid passing arguments in floating point registers,
   // since softfloat uses integer registers.
   template <auto F> constexpr auto choose_fn() {
      if constexpr (use_softfloat) {
         if constexpr (std::is_same_v<decltype(F), float (*)(float)>) {
            return &adapt_f32_unop<F>;
         } else if constexpr (std::is_same_v<decltype(F), float (*)(float, float)>) {
            return &adapt_f32_binop<F>;
         } else if constexpr (std::is_same_v<decltype(F), bool (*)(float, float)>) {
            return &adapt_f32_cmp<F>;
         } else if constexpr (std::is_same_v<decltype(F), double (*)(double)>) {
            return &adapt_f64_unop<F>;
         } else if constexpr (std::is_same_v<decltype(F), double (*)(double, double)>) {
            return &adapt_f64_binop<F>;
         } else if constexpr (std::is_same_v<decltype(F), bool (*)(double, double)>) {
            return &adapt_f64_cmp<F>;
         } else {
            return choose_unop<F>(F);
         }
      } else {
         return nullptr;
      }
   }

   template <auto F, typename R, typename... A> static R softfloat_trap_fn(A... a) {
      R result;
      longjmp_on_exception([&]() { result = F(a...); });
      return result;
   }

   template <auto F, typename R, typename... A>
   static constexpr auto make_softfloat_trap_fn(R (*)(A...)) -> R (*)(A...) {
      return softfloat_trap_fn<F, R, A...>;
   }

   template <auto F> static constexpr decltype(auto) softfloat_trap() {
      return *make_softfloat_trap_fn<F>(F);
   }

#define CHOOSE_FN(name) choose_fn<&name>()

   // --------------- f32 relops ----------------------
   void emit_f32_eq() {
      auto icount = softfloat_instr(25, 45, 59);
      emit_f32_relop(0x00, CHOOSE_FN(_wasm_f32_eq), false, false);
   }

   void emit_f32_ne() {
      auto icount = softfloat_instr(24, 47, 61);
      emit_f32_relop(0x00, CHOOSE_FN(_wasm_f32_eq), false, true);
   }

   void emit_f32_lt() {
      auto icount = softfloat_instr(25, 45, 59);
      emit_f32_relop(0x01, CHOOSE_FN(_wasm_f32_lt), false, false);
   }

   void emit_f32_gt() {
      auto icount = softfloat_instr(25, 45, 59);
      emit_f32_relop(0x01, CHOOSE_FN(_wasm_f32_lt), true, false);
   }

   void emit_f32_le() {
      auto icount = softfloat_instr(25, 45, 59);
      emit_f32_relop(0x02, CHOOSE_FN(_wasm_f32_le), false, false);
   }

   void emit_f32_ge() {
      auto icount = softfloat_instr(25, 45, 59);
      emit_f32_relop(0x02, CHOOSE_FN(_wasm_f32_le), true, false);
   }

   // --------------- f64 relops ----------------------
   void emit_f64_eq() {
      auto icount = softfloat_instr(25, 47, 61);
      emit_f64_relop(0x00, CHOOSE_FN(_wasm_f64_eq), false, false);
   }

   void emit_f64_ne() {
      auto icount = softfloat_instr(24, 49, 63);
      emit_f64_relop(0x00, CHOOSE_FN(_wasm_f64_eq), false, true);
   }

   void emit_f64_lt() {
      auto icount = softfloat_instr(25, 47, 61);
      emit_f64_relop(0x01, CHOOSE_FN(_wasm_f64_lt), false, false);
   }

   void emit_f64_gt() {
      auto icount = softfloat_instr(25, 47, 61);
      emit_f64_relop(0x01, CHOOSE_FN(_wasm_f64_lt), true, false);
   }

   void emit_f64_le() {
      auto icount = softfloat_instr(25, 47, 61);
      emit_f64_relop(0x02, CHOOSE_FN(_wasm_f64_le), false, false);
   }

   void emit_f64_ge() {
      auto icount = softfloat_instr(25, 47, 61);
      emit_f64_relop(0x02, CHOOSE_FN(_wasm_f64_le), true, false);
   }

   // --------------- i32 unops ----------------------

   bool has_tzcnt_impl() {
      unsigned a, b, c, d;
      return __get_cpuid_count(7, 0, &a, &b, &c, &d) && (b & bit_BMI) && __get_cpuid(0x80000001, &a, &b, &c, &d) &&
             (c & bit_LZCNT);
   }

   bool has_tzcnt() {
      static bool result = has_tzcnt_impl();
      return result;
   }

   void emit_i32_clz() {
      auto icount = fixed_size_instr(has_tzcnt() ? 6 : 18);
      if (!has_tzcnt()) {
         // pop %rax
         emit_bytes(0x58);
         // mov $-1, %ecx
         emit_bytes(0xb9, 0xff, 0xff, 0xff, 0xff);
         // bsr %eax, %eax
         emit_bytes(0x0f, 0xbd, 0xc0);
         // cmovz %ecx, %eax
         emit_bytes(0x0f, 0x44, 0xc1);
         // sub $31, %eax
         emit_bytes(0x83, 0xe8, 0x1f);
         // neg %eax
         emit_bytes(0xf7, 0xd8);
         // push %rax
         emit_bytes(0x50);
      } else {
         // popq %rax
         emit_bytes(0x58);
         // lzcntl %eax, %eax
         emit_bytes(0xf3, 0x0f, 0xbd, 0xc0);
         // pushq %rax
         emit_bytes(0x50);
      }
   }

   void emit_i32_ctz() {
      auto icount = fixed_size_instr(has_tzcnt() ? 6 : 13);
      if (!has_tzcnt()) {
         // pop %rax
         emit_bytes(0x58);
         // mov $32, %ecx
         emit_bytes(0xb9, 0x20, 0x00, 0x00, 0x00);
         // bsf %eax, %eax
         emit_bytes(0x0f, 0xbc, 0xc0);
         // cmovz %ecx, %eax
         emit_bytes(0x0f, 0x44, 0xc1);
         // push %rax
         emit_bytes(0x50);
      } else {
         // popq %rax
         emit_bytes(0x58);
         // tzcntl %eax, %eax
         emit_bytes(0xf3, 0x0f, 0xbc, 0xc0);
         // pushq %rax
         emit_bytes(0x50);
      }
   }

   void emit_i32_popcnt() {
      auto icount = fixed_size_instr(6);
      // popq %rax
      emit_bytes(0x58);
      // popcntl %eax, %eax
      emit_bytes(0xf3, 0x0f, 0xb8, 0xc0);
      // pushq %rax
      emit_bytes(0x50);
   }

   // --------------- i32 binops ----------------------

   void emit_i32_add() {
      auto icount = fixed_size_instr(5);
      emit_i32_binop(0x01, 0xc8, 0x50);
   }
   void emit_i32_sub() {
      auto icount = fixed_size_instr(5);
      emit_i32_binop(0x29, 0xc8, 0x50);
   }
   void emit_i32_mul() {
      auto icount = fixed_size_instr(6);
      emit_i32_binop(0x0f, 0xaf, 0xc1, 0x50);
   }
   // cdq; idiv %ecx; pushq %rax
   void emit_i32_div_s() {
      auto icount = fixed_size_instr(6);
      emit_i32_binop(0x99, 0xf7, 0xf9, 0x50);
   }
   void emit_i32_div_u() {
      auto icount = fixed_size_instr(7);
      emit_i32_binop(0x31, 0xd2, 0xf7, 0xf1, 0x50);
   }
   void emit_i32_rem_s() {
      auto icount = fixed_size_instr(22);
      // pop %rcx
      emit_bytes(0x59);
      // pop %rax
      emit_bytes(0x58);
      // cmp $-1, %edx
      emit_bytes(0x83, 0xf9, 0xff);
      // je MINUS1
      emit_bytes(0x0f, 0x84);
      void* minus1 = emit_branch_target32();
      // cdq
      emit_bytes(0x99);
      // idiv %ecx
      emit_bytes(0xf7, 0xf9);
      // jmp END
      emit_bytes(0xe9);
      void* end = emit_branch_target32();
      // MINUS1:
      fix_branch(minus1, code);
      // xor %edx, %edx
      emit_bytes(0x31, 0xd2);
      // END:
      fix_branch(end, code);
      // push %rdx
      emit_bytes(0x52);
   }
   void emit_i32_rem_u() {
      auto icount = fixed_size_instr(7);
      emit_i32_binop(0x31, 0xd2, 0xf7, 0xf1, 0x52);
   }
   void emit_i32_and() {
      auto icount = fixed_size_instr(5);
      emit_i32_binop(0x21, 0xc8, 0x50);
   }
   void emit_i32_or() {
      auto icount = fixed_size_instr(5);
      emit_i32_binop(0x09, 0xc8, 0x50);
   }
   void emit_i32_xor() {
      auto icount = fixed_size_instr(5);
      emit_i32_binop(0x31, 0xc8, 0x50);
   }
   void emit_i32_shl() {
      auto icount = fixed_size_instr(5);
      emit_i32_binop(0xd3, 0xe0, 0x50);
   }
   void emit_i32_shr_s() {
      auto icount = fixed_size_instr(5);
      emit_i32_binop(0xd3, 0xf8, 0x50);
   }
   void emit_i32_shr_u() {
      auto icount = fixed_size_instr(5);
      emit_i32_binop(0xd3, 0xe8, 0x50);
   }
   void emit_i32_rotl() {
      auto icount = fixed_size_instr(5);
      emit_i32_binop(0xd3, 0xc0, 0x50);
   }
   void emit_i32_rotr() {
      auto icount = fixed_size_instr(5);
      emit_i32_binop(0xd3, 0xc8, 0x50);
   }

   // --------------- i64 unops ----------------------

   void emit_i64_clz() {
      auto icount = fixed_size_instr(has_tzcnt() ? 7 : 24);
      if (!has_tzcnt()) {
         // pop %rax
         emit_bytes(0x58);
         // mov $-1, %ecx
         emit_bytes(0x48, 0xc7, 0xc1, 0xff, 0xff, 0xff, 0xff);
         // bsr %eax, %eax
         emit_bytes(0x48, 0x0f, 0xbd, 0xc0);
         // cmovz %ecx, %eax
         emit_bytes(0x48, 0x0f, 0x44, 0xc1);
         // sub $63, %eax
         emit_bytes(0x48, 0x83, 0xe8, 0x3f);
         // neg %eax
         emit_bytes(0x48, 0xf7, 0xd8);
         // push %rax
         emit_bytes(0x50);
      } else {
         // popq %rax
         emit_bytes(0x58);
         // lzcntq %eax, %eax
         emit_bytes(0xf3, 0x48, 0x0f, 0xbd, 0xc0);
         // pushq %rax
         emit_bytes(0x50);
      }
   }

   void emit_i64_ctz() {
      auto icount = fixed_size_instr(has_tzcnt() ? 7 : 17);
      if (!has_tzcnt()) {
         // pop %rax
         emit_bytes(0x58);
         // mov $64, %ecx
         emit_bytes(0x48, 0xc7, 0xc1, 0x40, 0x00, 0x00, 0x00);
         // bsf %eax, %eax
         emit_bytes(0x48, 0x0f, 0xbc, 0xc0);
         // cmovz %ecx, %eax
         emit_bytes(0x48, 0x0f, 0x44, 0xc1);
         // push %rax
         emit_bytes(0x50);
      } else {
         // popq %rax
         emit_bytes(0x58);
         // tzcntq %eax, %eax
         emit_bytes(0xf3, 0x48, 0x0f, 0xbc, 0xc0);
         // pushq %rax
         emit_bytes(0x50);
      }
   }

   void emit_i64_popcnt() {
      auto icount = fixed_size_instr(7);
      // popq %rax
      emit_bytes(0x58);
      // popcntq %rax, %rax
      emit_bytes(0xf3, 0x48, 0x0f, 0xb8, 0xc0);
      // pushq %rax
      emit_bytes(0x50);
   }

   // --------------- i64 binops ----------------------

   void emit_i64_add() {
      auto icount = fixed_size_instr(6);
      emit_i64_binop(0x48, 0x01, 0xc8, 0x50);
   }
   void emit_i64_sub() {
      auto icount = fixed_size_instr(6);
      emit_i64_binop(0x48, 0x29, 0xc8, 0x50);
   }
   void emit_i64_mul() {
      auto icount = fixed_size_instr(7);
      emit_i64_binop(0x48, 0x0f, 0xaf, 0xc1, 0x50);
   }
   // cdq; idiv %rcx; pushq %rax
   void emit_i64_div_s() {
      auto icount = fixed_size_instr(8);
      emit_i64_binop(0x48, 0x99, 0x48, 0xf7, 0xf9, 0x50);
   }
   void emit_i64_div_u() {
      auto icount = fixed_size_instr(9);
      emit_i64_binop(0x48, 0x31, 0xd2, 0x48, 0xf7, 0xf1, 0x50);
   }
   void emit_i64_rem_s() {
      auto icount = fixed_size_instr(25);
      // pop %rcx
      emit_bytes(0x59);
      // pop %rax
      emit_bytes(0x58);
      // cmp $-1, %rcx
      emit_bytes(0x48, 0x83, 0xf9, 0xff);
      // je MINUS1
      emit_bytes(0x0f, 0x84);
      void* minus1 = emit_branch_target32();
      // cqo
      emit_bytes(0x48, 0x99);
      // idiv %rcx
      emit_bytes(0x48, 0xf7, 0xf9);
      // jmp END
      emit_bytes(0xe9);
      void* end = emit_branch_target32();
      // MINUS1:
      fix_branch(minus1, code);
      // xor %edx, %edx
      emit_bytes(0x31, 0xd2);
      // END:
      fix_branch(end, code);
      // push %rdx
      emit_bytes(0x52);
   }
   void emit_i64_rem_u() {
      auto icount = fixed_size_instr(9);
      emit_i64_binop(0x48, 0x31, 0xd2, 0x48, 0xf7, 0xf1, 0x52);
   }
   void emit_i64_and() {
      auto icount = fixed_size_instr(6);
      emit_i64_binop(0x48, 0x21, 0xc8, 0x50);
   }
   void emit_i64_or() {
      auto icount = fixed_size_instr(6);
      emit_i64_binop(0x48, 0x09, 0xc8, 0x50);
   }
   void emit_i64_xor() {
      auto icount = fixed_size_instr(6);
      emit_i64_binop(0x48, 0x31, 0xc8, 0x50);
   }
   void emit_i64_shl() {
      auto icount = fixed_size_instr(6);
      emit_i64_binop(0x48, 0xd3, 0xe0, 0x50);
   }
   void emit_i64_shr_s() {
      auto icount = fixed_size_instr(6);
      emit_i64_binop(0x48, 0xd3, 0xf8, 0x50);
   }
   void emit_i64_shr_u() {
      auto icount = fixed_size_instr(6);
      emit_i64_binop(0x48, 0xd3, 0xe8, 0x50);
   }
   void emit_i64_rotl() {
      auto icount = fixed_size_instr(6);
      emit_i64_binop(0x48, 0xd3, 0xc0, 0x50);
   }
   void emit_i64_rotr() {
      auto icount = fixed_size_instr(6);
      emit_i64_binop(0x48, 0xd3, 0xc8, 0x50);
   }

   // --------------- f32 unops ----------------------

   void emit_f32_abs() {
      auto icount = fixed_size_instr(7);
      // popq %rax;
      emit_bytes(0x58);
      // andl 0x7fffffff, %eax
      emit_bytes(0x25);
      emit_operand32(0x7fffffff);
      // pushq %rax
      emit_bytes(0x50);
   }

   void emit_f32_neg() {
      auto icount = fixed_size_instr(7);
      // popq %rax
      emit_bytes(0x58);
      // xorl 0x80000000, %eax
      emit_bytes(0x35);
      emit_operand32(0x80000000);
      // pushq %rax
      emit_bytes(0x50);
   }

   void emit_f32_ceil() {
      auto icount = softfloat_instr(12, 36, 54);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f32_ceil));
      }
      // roundss 0b1010, (%rsp), %xmm0
      emit_bytes(0x66, 0x0f, 0x3a, 0x0a, 0x04, 0x24, 0x0a);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_f32_floor() {
      auto icount = softfloat_instr(12, 36, 54);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f32_floor));
      }
      // roundss 0b1001, (%rsp), %xmm0
      emit_bytes(0x66, 0x0f, 0x3a, 0x0a, 0x04, 0x24, 0x09);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_f32_trunc() {
      auto icount = softfloat_instr(12, 36, 54);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f32_trunc));
      }
      // roundss 0b1011, (%rsp), %xmm0
      emit_bytes(0x66, 0x0f, 0x3a, 0x0a, 0x04, 0x24, 0x0b);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_f32_nearest() {
      auto icount = softfloat_instr(12, 36, 54);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f32_nearest));
      }
      // roundss 0b1000, (%rsp), %xmm0
      emit_bytes(0x66, 0x0f, 0x3a, 0x0a, 0x04, 0x24, 0x08);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_f32_sqrt() {
      auto icount = softfloat_instr(10, 36, 54);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f32_sqrt));
      }
      // sqrtss (%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x51, 0x04, 0x24);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }

   // --------------- f32 binops ----------------------

   void emit_f32_add() {
      auto icount = softfloat_instr(21, 44, 58);
      emit_f32_binop(0x58, CHOOSE_FN(_wasm_f32_add));
   }
   void emit_f32_sub() {
      auto icount = softfloat_instr(21, 44, 58);
      emit_f32_binop(0x5c, CHOOSE_FN(_wasm_f32_sub));
   }
   void emit_f32_mul() {
      auto icount = softfloat_instr(21, 44, 58);
      emit_f32_binop(0x59, CHOOSE_FN(_wasm_f32_mul));
   }
   void emit_f32_div() {
      auto icount = softfloat_instr(21, 44, 58);
      emit_f32_binop(0x5e, CHOOSE_FN(_wasm_f32_div));
   }
   void emit_f32_min() {
      auto icount = softfloat_instr(47, 44, 58);
      if constexpr (use_softfloat) {
         emit_f32_binop_softfloat(CHOOSE_FN(_wasm_f32_min));
         return;
      }
      // mov (%rsp), %eax
      emit_bytes(0x8b, 0x04, 0x24);
      // test %eax, %eax
      emit_bytes(0x85, 0xc0);
      // je ZERO
      emit_bytes(0x0f, 0x84);
      void* zero = emit_branch_target32();
      // movss 8(%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x10, 0x44, 0x24, 0x08);
      // minss (%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x5d, 0x04, 0x24);
      // jmp DONE
      emit_bytes(0xe9);
      void* done = emit_branch_target32();
      // ZERO:
      fix_branch(zero, code);
      // movss (%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x10, 0x04, 0x24);
      // minss 8(%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x5d, 0x44, 0x24, 0x08);
      // DONE:
      fix_branch(done, code);
      // add $8, %rsp
      emit_bytes(0x48, 0x83, 0xc4, 0x08);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }
   void emit_f32_max() {
      auto icount = softfloat_instr(47, 44, 58);
      if (use_softfloat) {
         emit_f32_binop_softfloat(CHOOSE_FN(_wasm_f32_max));
         return;
      }
      // mov (%rsp), %eax
      emit_bytes(0x8b, 0x04, 0x24);
      // test %eax, %eax
      emit_bytes(0x85, 0xc0);
      // je ZERO
      emit_bytes(0x0f, 0x84);
      void* zero = emit_branch_target32();
      // movss (%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x10, 0x04, 0x24);
      // maxss 8(%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x5f, 0x44, 0x24, 0x08);
      // jmp DONE
      emit_bytes(0xe9);
      void* done = emit_branch_target32();
      // ZERO:
      fix_branch(zero, code);
      // movss 8(%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x10, 0x44, 0x24, 0x08);
      // maxss (%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x5f, 0x04, 0x24);
      // DONE:
      fix_branch(done, code);
      // add $8, %rsp
      emit_bytes(0x48, 0x83, 0xc4, 0x08);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_f32_copysign() {
      auto icount = fixed_size_instr(16);
      // popq %rax;
      emit_bytes(0x58);
      // andl 0x80000000, %eax
      emit_bytes(0x25);
      emit_operand32(0x80000000);
      // popq %rcx
      emit_bytes(0x59);
      // andl 0x7fffffff, %ecx
      emit_bytes(0x81, 0xe1);
      emit_operand32(0x7fffffff);
      // orl %ecx, %eax
      emit_bytes(0x09, 0xc8);
      // pushq %rax
      emit_bytes(0x50);
   }

   // --------------- f64 unops ----------------------

   void emit_f64_abs() {
      auto icount = fixed_size_instr(15);
      // popq %rcx;
      emit_bytes(0x59);
      // movabsq $0x7fffffffffffffff, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand64(0x7fffffffffffffffull);
      // andq %rcx, %rax
      emit_bytes(0x48, 0x21, 0xc8);
      // pushq %rax
      emit_bytes(0x50);
   }

   void emit_f64_neg() {
      auto icount = fixed_size_instr(15);
      // popq %rcx;
      emit_bytes(0x59);
      // movabsq $0x8000000000000000, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand64(0x8000000000000000ull);
      // xorq %rcx, %rax
      emit_bytes(0x48, 0x31, 0xc8);
      // pushq %rax
      emit_bytes(0x50);
   }

   void emit_f64_ceil() {
      auto icount = softfloat_instr(12, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f64_ceil));
      }
      // roundsd 0b1010, (%rsp), %xmm0
      emit_bytes(0x66, 0x0f, 0x3a, 0x0b, 0x04, 0x24, 0x0a);
      // movsd %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_f64_floor() {
      auto icount = softfloat_instr(12, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f64_floor));
      }
      // roundsd 0b1001, (%rsp), %xmm0
      emit_bytes(0x66, 0x0f, 0x3a, 0x0b, 0x04, 0x24, 0x09);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_f64_trunc() {
      auto icount = softfloat_instr(12, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f64_trunc));
      }
      // roundsd 0b1011, (%rsp), %xmm0
      emit_bytes(0x66, 0x0f, 0x3a, 0x0b, 0x04, 0x24, 0x0b);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_f64_nearest() {
      auto icount = softfloat_instr(12, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f64_nearest));
      }
      // roundsd 0b1000, (%rsp), %xmm0
      emit_bytes(0x66, 0x0f, 0x3a, 0x0b, 0x04, 0x24, 0x08);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_f64_sqrt() {
      auto icount = softfloat_instr(10, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f64_sqrt));
      }
      // sqrtss (%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x51, 0x04, 0x24);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }

   // --------------- f64 binops ----------------------

   void emit_f64_add() {
      auto icount = softfloat_instr(21, 47, 61);
      emit_f64_binop(0x58, CHOOSE_FN(_wasm_f64_add));
   }
   void emit_f64_sub() {
      auto icount = softfloat_instr(21, 47, 61);
      emit_f64_binop(0x5c, CHOOSE_FN(_wasm_f64_sub));
   }
   void emit_f64_mul() {
      auto icount = softfloat_instr(21, 47, 61);
      emit_f64_binop(0x59, CHOOSE_FN(_wasm_f64_mul));
   }
   void emit_f64_div() {
      auto icount = softfloat_instr(21, 47, 61);
      emit_f64_binop(0x5e, CHOOSE_FN(_wasm_f64_div));
   }
   void emit_f64_min() {
      auto icount = softfloat_instr(49, 47, 61);
      if (use_softfloat) {
         emit_f64_binop_softfloat(CHOOSE_FN(_wasm_f64_min));
         return;
      }
      // mov (%rsp), %rax
      emit_bytes(0x48, 0x8b, 0x04, 0x24);
      // test %rax, %rax
      emit_bytes(0x48, 0x85, 0xc0);
      // je ZERO
      emit_bytes(0x0f, 0x84);
      void* zero = emit_branch_target32();
      // movsd 8(%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x10, 0x44, 0x24, 0x08);
      // minsd (%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x5d, 0x04, 0x24);
      // jmp DONE
      emit_bytes(0xe9);
      void* done = emit_branch_target32();
      // ZERO:
      fix_branch(zero, code);
      // movsd (%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x10, 0x04, 0x24);
      // minsd 8(%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x5d, 0x44, 0x24, 0x08);
      // DONE:
      fix_branch(done, code);
      // add $8, %rsp
      emit_bytes(0x48, 0x83, 0xc4, 0x08);
      // movsd %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }
   void emit_f64_max() {
      auto icount = softfloat_instr(49, 47, 61);
      if (use_softfloat) {
         emit_f64_binop_softfloat(CHOOSE_FN(_wasm_f64_max));
         return;
      }
      // mov (%rsp), %rax
      emit_bytes(0x48, 0x8b, 0x04, 0x24);
      // test %rax, %rax
      emit_bytes(0x48, 0x85, 0xc0);
      // je ZERO
      emit_bytes(0x0f, 0x84);
      void* zero = emit_branch_target32();
      // maxsd (%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x10, 0x04, 0x24);
      // maxsd 8(%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x5f, 0x44, 0x24, 0x08);
      // jmp DONE
      emit_bytes(0xe9);
      void* done = emit_branch_target32();
      // ZERO:
      fix_branch(zero, code);
      // movsd 8(%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x10, 0x44, 0x24, 0x08);
      // maxsd (%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x5f, 0x04, 0x24);
      // DONE:
      fix_branch(done, code);
      // add $8, %rsp
      emit_bytes(0x48, 0x83, 0xc4, 0x08);
      // movsd %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_f64_copysign() {
      auto icount = fixed_size_instr(25);
      // popq %rcx;
      emit_bytes(0x59);
      // movabsq 0x8000000000000000, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand64(0x8000000000000000ull);
      // andq %rax, %rcx
      emit_bytes(0x48, 0x21, 0xc1);
      // popq %rdx
      emit_bytes(0x5a);
      // notq %rax
      emit_bytes(0x48, 0xf7, 0xd0);
      // andq %rdx, %rax
      emit_bytes(0x48, 0x21, 0xd0);
      // orq %rcx, %rax
      emit_bytes(0x48, 0x09, 0xc8);
      // pushq %rax
      emit_bytes(0x50);
   }

   // --------------- conversions --------------------

   void emit_i32_wrap_i64() {
      auto icount = fixed_size_instr(6);
      // Zero out the high 4 bytes
      // xor %eax, %eax
      emit_bytes(0x31, 0xc0);
      // mov %eax, 4(%rsp)
      emit_bytes(0x89, 0x44, 0x24, 0x04);
   }

   void emit_i32_trunc_s_f32() {
      auto icount = softfloat_instr(33, 36, 54);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_wasm_f32_trunc_i32s>()));
      }
      // cvttss2si 8(%rsp), %eax
      emit_f2i(0xf3, 0x0f, 0x2c, 0x44, 0x24, 0x08);
      // mov %eax, (%rsp)
      emit_bytes(0x89, 0x04, 0x24);
   }

   void emit_i32_trunc_u_f32() {
      auto icount = softfloat_instr(46, 36, 54);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_wasm_f32_trunc_i32u>()));
      }
      // cvttss2si 8(%rsp), %rax
      emit_f2i(0xf3, 0x48, 0x0f, 0x2c, 0x44, 0x24, 0x08);
      // mov %eax, (%rsp)
      emit_bytes(0x89, 0x04, 0x24);
      // shr $32, %rax
      emit_bytes(0x48, 0xc1, 0xe8, 0x20);
      // test %eax, %eax
      emit_bytes(0x85, 0xc0);
      // jnz FP_ERROR_HANDLER
      emit_bytes(0x0f, 0x85);
      fix_branch(emit_branch_target32(), fpe_handler);
   }
   void emit_i32_trunc_s_f64() {
      auto icount = softfloat_instr(34, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_wasm_f64_trunc_i32s>()));
      }
      // cvttsd2si 8(%rsp), %eax
      emit_f2i(0xf2, 0x0f, 0x2c, 0x44, 0x24, 0x08);
      // movq %rax, (%rsp)
      emit_bytes(0x48, 0x89, 0x04, 0x24);
   }

   void emit_i32_trunc_u_f64() {
      auto icount = softfloat_instr(47, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_wasm_f64_trunc_i32u>()));
      }
      // cvttsd2si 8(%rsp), %rax
      emit_f2i(0xf2, 0x48, 0x0f, 0x2c, 0x44, 0x24, 0x08);
      // movq %rax, (%rsp)
      emit_bytes(0x48, 0x89, 0x04, 0x24);
      // shr $32, %rax
      emit_bytes(0x48, 0xc1, 0xe8, 0x20);
      // test %eax, %eax
      emit_bytes(0x85, 0xc0);
      // jnz FP_ERROR_HANDLER
      emit_bytes(0x0f, 0x85);
      fix_branch(emit_branch_target32(), fpe_handler);
   }

   void emit_i64_extend_s_i32() {
      auto icount = fixed_size_instr(8);
      // movslq (%rsp), %rax
      emit_bytes(0x48, 0x63, 0x04, 0x24);
      // mov %rax, (%rsp)
      emit_bytes(0x48, 0x89, 0x04, 0x24);
   }

   void emit_i64_extend_u_i32() { /* Nothing to do */ }

   void emit_i64_trunc_s_f32() {
      auto icount = softfloat_instr(35, 37, 55);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_wasm_f32_trunc_i64s>()));
      }
      // cvttss2si (%rsp), %rax
      emit_f2i(0xf3, 0x48, 0x0f, 0x2c, 0x44, 0x24, 0x08);
      // mov %rax, (%rsp)
      emit_bytes(0x48, 0x89, 0x04, 0x24);
   }
   void emit_i64_trunc_u_f32() {
      auto icount = softfloat_instr(101, 37, 55);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_wasm_f32_trunc_i64u>()));
      }
      // mov $0x5f000000, %eax
      emit_bytes(0xb8);
      emit_operand32(0x5f000000);
      // movss (%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x10, 0x04, 0x24);
      // mov %eax, (%rsp)
      emit_bytes(0x89, 0x04, 0x24);
      // movss (%rsp), %xmm1
      emit_bytes(0xf3, 0x0f, 0x10, 0x0c, 0x24);
      // movaps %xmm0, %xmm2
      emit_bytes(0x0f, 0x28, 0xd0);
      // subss %xmm1, %xmm2
      emit_bytes(0xf3, 0x0f, 0x5c, 0xd1);
      // cvttss2siq %xmm2, %rax
      emit_f2i(0xf3, 0x48, 0x0f, 0x2c, 0xc2);
      // movabsq $0x8000000000000000, %rcx
      emit_bytes(0x48, 0xb9);
      emit_operand64(0x8000000000000000);
      // xorq %rax, %rcx
      emit_bytes(0x48, 0x31, 0xc1);
      // cvttss2siq %xmm0, %rax
      emit_bytes(0xf3, 0x48, 0x0f, 0x2c, 0xc0);
      // xor %rdx, %rdx
      emit_bytes(0x48, 0x31, 0xd2);
      // ucomiss %xmm0, %xmm1
      emit_bytes(0x0f, 0x2e, 0xc8);
      // cmovaq %rax, %rdx
      emit_bytes(0x48, 0x0f, 0x47, 0xd0);
      // cmovbeq %rcx, %rax
      emit_bytes(0x48, 0x0f, 0x46, 0xc1);
      // mov %rax, (%rsp)
      emit_bytes(0x48, 0x89, 0x04, 0x24);
      // bt $63, %rdx
      emit_bytes(0x48, 0x0f, 0xba, 0xe2, 0x3f);
      // jc FP_ERROR_HANDLER
      emit_bytes(0x0f, 0x82);
      fix_branch(emit_branch_target32(), fpe_handler);
   }
   void emit_i64_trunc_s_f64() {
      auto icount = softfloat_instr(35, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_wasm_f64_trunc_i64s>()));
      }
      // cvttsd2si (%rsp), %rax
      emit_f2i(0xf2, 0x48, 0x0f, 0x2c, 0x44, 0x24, 0x08);
      // mov %rax, (%rsp)
      emit_bytes(0x48, 0x89, 0x04, 0x24);
   }
   void emit_i64_trunc_u_f64() {
      auto icount = softfloat_instr(109, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(softfloat_trap<&_wasm_f64_trunc_i64u>()));
      }
      // movabsq $0x43e0000000000000, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand64(0x43e0000000000000);
      // movsd (%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x10, 0x04, 0x24);
      // movq %rax, (%rsp)
      emit_bytes(0x48, 0x89, 0x04, 0x24);
      // movsd (%rsp), %xmm1
      emit_bytes(0xf2, 0x0f, 0x10, 0x0c, 0x24);
      // movapd %xmm0, %xmm2
      emit_bytes(0x66, 0x0f, 0x28, 0xd0);
      // subsd %xmm1, %xmm2
      emit_bytes(0xf2, 0x0f, 0x5c, 0xd1);
      // cvttsd2siq %xmm2, %rax
      emit_f2i(0xf2, 0x48, 0x0f, 0x2c, 0xc2);
      // movabsq $0x8000000000000000, %rcx
      emit_bytes(0x48, 0xb9);
      emit_operand64(0x8000000000000000);
      // xorq %rax, %rcx
      emit_bytes(0x48, 0x31, 0xc1);
      // cvttsd2siq %xmm0, %rax
      emit_bytes(0xf2, 0x48, 0x0f, 0x2c, 0xc0);
      // xor %rdx, %rdx
      emit_bytes(0x48, 0x31, 0xd2);
      // ucomisd %xmm0, %xmm1
      emit_bytes(0x66, 0x0f, 0x2e, 0xc8);
      // cmovaq %rax, %rdx
      emit_bytes(0x48, 0x0f, 0x47, 0xd0);
      // cmovbeq %rcx, %rax
      emit_bytes(0x48, 0x0f, 0x46, 0xc1);
      // mov %rax, (%rsp)
      emit_bytes(0x48, 0x89, 0x04, 0x24);
      // bt $63, %rdx
      emit_bytes(0x48, 0x0f, 0xba, 0xe2, 0x3f);
      // jc FP_ERROR_HANDLER
      emit_bytes(0x0f, 0x82);
      fix_branch(emit_branch_target32(), fpe_handler);
   }

   void emit_f32_convert_s_i32() {
      auto icount = softfloat_instr(10, 36, 54);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_i32_to_f32));
      }
      // cvtsi2ssl (%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x2a, 0x04, 0x24);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }
   void emit_f32_convert_u_i32() {
      auto icount = softfloat_instr(11, 36, 54);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_ui32_to_f32));
      }
      // zero-extend to 64-bits
      // cvtsi2sslq (%rsp), %xmm0
      emit_bytes(0xf3, 0x48, 0x0f, 0x2a, 0x04, 0x24);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }
   void emit_f32_convert_s_i64() {
      auto icount = softfloat_instr(11, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_i64_to_f32));
      }
      // cvtsi2sslq (%rsp), %xmm0
      emit_bytes(0xf3, 0x48, 0x0f, 0x2a, 0x04, 0x24);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }
   void emit_f32_convert_u_i64() {
      auto icount = softfloat_instr(55, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_ui64_to_f32));
      }
      // movq (%rsp), %rax
      emit_bytes(0x48, 0x8b, 0x04, 0x24);
      // testq %rax, %rax
      emit_bytes(0x48, 0x85, 0xc0);
      // js LARGE
      emit_bytes(0x0f, 0x88);
      void* large = emit_branch_target32();
      // cvtsi2ssq %rax, %xmm0
      emit_bytes(0xf3, 0x48, 0x0f, 0x2a, 0xc0);
      // jmp done
      emit_bytes(0xe9);
      void* done = emit_branch_target32();
      // LARGE:
      fix_branch(large, code);
      // movq %rax, %rcx
      emit_bytes(0x48, 0x89, 0xc1);
      // shrq %rax
      emit_bytes(0x48, 0xd1, 0xe8);
      // andl $1, %ecx
      emit_bytes(0x83, 0xe1, 0x01);
      // orq %rcx, %rax
      emit_bytes(0x48, 0x09, 0xc8);
      // cvtsi2ssq %rax, %xmm0
      emit_bytes(0xf3, 0x48, 0x0f, 0x2a, 0xc0);
      // addss %xmm0, %xmm0
      emit_bytes(0xf3, 0x0f, 0x58, 0xc0);
      // DONE:
      fix_branch(done, code);
      // xorl %eax, %eax
      emit_bytes(0x31, 0xc0);
      // movl %eax, 4(%rsp)
      emit_bytes(0x89, 0x44, 0x24, 0x04);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }
   void emit_f32_demote_f64() {
      auto icount = softfloat_instr(16, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f64_demote));
      }
      // cvtsd2ss (%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x5a, 0x04, 0x24);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
      // Zero out the high 4 bytes
      // xor %eax, %eax
      emit_bytes(0x31, 0xc0);
      // mov %eax, 4(%rsp)
      emit_bytes(0x89, 0x44, 0x24, 0x04);
   }
   void emit_f64_convert_s_i32() {
      auto icount = softfloat_instr(10, 37, 55);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_i32_to_f64));
      }
      // cvtsi2sdl (%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x2a, 0x04, 0x24);
      // movsd %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }
   void emit_f64_convert_u_i32() {
      auto icount = softfloat_instr(11, 37, 55);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_ui32_to_f64));
      }
      //  cvtsi2sdq (%rsp), %xmm0
      emit_bytes(0xf2, 0x48, 0x0f, 0x2a, 0x04, 0x24);
      // movsd %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }
   void emit_f64_convert_s_i64() {
      auto icount = softfloat_instr(11, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_i64_to_f64));
      }
      //  cvtsi2sdq (%rsp), %xmm0
      emit_bytes(0xf2, 0x48, 0x0f, 0x2a, 0x04, 0x24);
      // movsd %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }
   void emit_f64_convert_u_i64() {
      auto icount = softfloat_instr(49, 38, 56);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_ui64_to_f64));
      }
      // movq (%rsp), %rax
      emit_bytes(0x48, 0x8b, 0x04, 0x24);
      // testq %rax, %rax
      emit_bytes(0x48, 0x85, 0xc0);
      // js LARGE
      emit_bytes(0x0f, 0x88);
      void* large = emit_branch_target32();
      // cvtsi2sdq %rax, %xmm0
      emit_bytes(0xf2, 0x48, 0x0f, 0x2a, 0xc0);
      // jmp done
      emit_bytes(0xe9);
      void* done = emit_branch_target32();
      // LARGE:
      fix_branch(large, code);
      // movq %rax, %rcx
      emit_bytes(0x48, 0x89, 0xc1);
      // shrq %rax
      emit_bytes(0x48, 0xd1, 0xe8);
      // andl $1, %ecx
      emit_bytes(0x83, 0xe1, 0x01);
      // orq %rcx, %rax
      emit_bytes(0x48, 0x09, 0xc8);
      // cvtsi2sdq %rax, %xmm0
      emit_bytes(0xf2, 0x48, 0x0f, 0x2a, 0xc0);
      // addsd %xmm0, %xmm0
      emit_bytes(0xf2, 0x0f, 0x58, 0xc0);
      // DONE:
      fix_branch(done, code);
      // movsd %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }
   void emit_f64_promote_f32() {
      auto icount = softfloat_instr(10, 37, 55);
      if constexpr (use_softfloat) {
         return emit_softfloat_unop(CHOOSE_FN(_wasm_f32_promote));
      }
      // cvtss2sd (%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x5a, 0x04, 0x24);
      // movsd %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_i32_reinterpret_f32() { /* Nothing to do */ }
   void emit_i64_reinterpret_f64() { /* Nothing to do */ }
   void emit_f32_reinterpret_i32() { /* Nothing to do */ }
   void emit_f64_reinterpret_i64() { /* Nothing to do */ }

#undef CHOOSE_FN

   void emit_error() {
      unimplemented();
   }

   // --------------- random  ------------------------
   static void fix_branch(void* branch, void* target) {
      auto branch_ = static_cast<uint8_t*>(branch);
      auto target_ = static_cast<uint8_t*>(target);
      auto relative = static_cast<uint32_t>(target_ - (branch_ + 4));
      if ((target_ - (branch_ + 4)) > 0x7FFFFFFFll || (target_ - (branch_ + 4)) < -0x80000000ll)
         unimplemented();
      memcpy(branch, &relative, 4);
   }

   // A 64-bit absolute address is used for function calls whose
   // address is too far away for a 32-bit relative call.
   static void fix_branch64(void* branch, void* target) {
      memcpy(branch, &target, 8);
   }

   using fn_type = native_value (*)(void* context, void* memory);
   void finalize(function_body& body) {
      _allocator.reclaim(code, _code_end - code);
      body.jit_code_offset = _code_start - (unsigned char*)_code_segment_base;
   }

   // returns the current write address
   const void* get_addr() const {
      return code;
   }

   const void* get_base_addr() const {
      return _code_segment_base;
   }

 private:
   auto fixed_size_instr(std::size_t expected_bytes) {
      return scope_guard{[this, expected_code = code + expected_bytes]() {
#ifdef FORGE_VM_WASM_VALIDATE_JIT_SIZE
         assert(code == expected_code);
#endif
         ignore_unused_variable_warning(code, expected_code);
      }};
   }
   auto variable_size_instr(std::size_t min, std::size_t max) {
      return scope_guard{[this, min_code = code + min, max_code = code + max]() {
#ifdef FORGE_VM_WASM_VALIDATE_JIT_SIZE
         assert(min_code <= code && code <= max_code);
#endif
         ignore_unused_variable_warning(code, min_code, max_code);
      }};
   }
   auto softfloat_instr(std::size_t hard_expected, std::size_t soft_expected, std::size_t softbt_expected) {
      return fixed_size_instr(use_softfloat ? (Context::async_backtrace() ? softbt_expected : soft_expected)
                                            : hard_expected);
   }

   module& _mod;
   growable_allocator& _allocator;
   void* _code_segment_base;
   const func_type* _ft;
   unsigned char* _code_start;
   unsigned char* _code_end;
   unsigned char* code;
   std::vector<std::variant<std::vector<void*>, void*>> _function_relocations;
   void* fpe_handler;
   void* call_indirect_handler;
   void* type_error_handler;
   void* stack_overflow_handler;
   void* jmp_table;
   uint32_t _local_count;
   uint32_t _table_element_size;

   void emit_byte(uint8_t val) {
      *code++ = val;
   }
   void emit_bytes() {}
   template <class... T> void emit_bytes(uint8_t val0, T... vals) {
      emit_byte(val0);
      emit_bytes(vals...);
   }
   void emit_operand32(uint32_t val) {
      memcpy(code, &val, sizeof(val));
      code += sizeof(val);
   }
   void emit_operand64(uint64_t val) {
      memcpy(code, &val, sizeof(val));
      code += sizeof(val);
   }
   void emit_operandf32(float val) {
      memcpy(code, &val, sizeof(val));
      code += sizeof(val);
   }
   void emit_operandf64(double val) {
      memcpy(code, &val, sizeof(val));
      code += sizeof(val);
   }
   template <class T> void emit_operand_ptr(T* val) {
      memcpy(code, &val, sizeof(val));
      code += sizeof(val);
   }

   void* emit_branch_target32() {
      void* result = code;
      emit_operand32(3735928555u - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(code)));
      return result;
   }

   void emit_check_call_depth() {
      // decl %ebx
      emit_bytes(0xff, 0xcb);
      // jz stack_overflow
      emit_bytes(0x0f, 0x84);
      fix_branch(emit_branch_target32(), stack_overflow_handler);
   }
   void emit_check_call_depth_end() {
      // incl %ebx
      emit_bytes(0xff, 0xc3);
   }

   static void unimplemented() {
      detail::fail<exceptions::parse>("Sorry, not implemented.");
   }

   // clobbers %rax if the high bit of count is set.
   void emit_multipop(uint32_t count) {
      if (count > 0 && count != 0x80000001) {
         if (count & 0x80000000) {
            // mov (%rsp), %rax
            emit_bytes(0x48, 0x8b, 0x04, 0x24);
         }
         if (count & 0x70000000) {
            // This code is probably unreachable.
            // int3
            emit_bytes(0xCC);
         }
         // add depth_change*8, %rsp
         emit_bytes(0x48, 0x81, 0xc4); // TODO: Prefer imm8 where appropriate
         emit_operand32(count * 8);    // FIXME: handle overflow
         if (count & 0x80000000) {
            // push %rax
            emit_bytes(0x50);
         }
      }
   }

   template <class... T> void emit_load_impl(uint32_t offset, T... loadop) {
      // pop %rax
      emit_bytes(0x58);
      if (offset & 0x80000000) {
         // mov $offset, %ecx
         emit_bytes(0xb9);
         emit_operand32(offset);
         // add %rcx, %rax
         emit_bytes(0x48, 0x01, 0xc8);
      } else if (offset != 0) {
         // add offset, %rax
         emit_bytes(0x48, 0x05);
         emit_operand32(offset);
      }
      // add %rsi, %rax
      emit_bytes(0x48, 0x01, 0xf0);
      // from the caller
      emit_bytes(static_cast<uint8_t>(loadop)...);
      // push RAX
      emit_bytes(0x50);
   }

   template <class... T> void emit_store_impl(uint32_t offset, T... storeop) {
      // pop RCX
      emit_bytes(0x59);
      // pop RAX
      emit_bytes(0x58);
      if (offset & 0x80000000) {
         // mov $offset, %ecx
         emit_bytes(0xb9);
         emit_operand32(offset);
         // add %rcx, %rax
         emit_bytes(0x48, 0x01, 0xc8);
      } else if (offset != 0) {
         // add offset, %rax
         emit_bytes(0x48, 0x05);
         emit_operand32(offset);
      }
      // add %rsi, %rax
      emit_bytes(0x48, 0x01, 0xf0);
      // from the caller
      emit_bytes(static_cast<uint8_t>(storeop)...);
      ;
   }

   void emit_i32_relop(uint8_t opcode) {
      // popq %rax
      emit_bytes(0x58);
      // popq %rcx
      emit_bytes(0x59);
      // xorq %rdx, %rdx
      emit_bytes(0x48, 0x31, 0xd2);
      // cmpl %eax, %ecx
      emit_bytes(0x39, 0xc1);
      // SETcc %dl
      emit_bytes(0x0f, opcode, 0xc2);
      // pushq %rdx
      emit_bytes(0x52);
   }

   template <class... T> void emit_i64_relop(uint8_t opcode) {
      // popq %rax
      emit_bytes(0x58);
      // popq %rcx
      emit_bytes(0x59);
      // xorq %rdx, %rdx
      emit_bytes(0x48, 0x31, 0xd2);
      // cmpq %rax, %rcx
      emit_bytes(0x48, 0x39, 0xc1);
      // SETcc %dl
      emit_bytes(0x0f, opcode, 0xc2);
      // pushq %rdx
      emit_bytes(0x52);
   }

   template <typename T, typename U> void emit_softfloat_unop(T (*softfloatfun)(U)) {
      auto extra = emit_setup_backtrace();
      // pushq %rdi
      emit_bytes(0x57);
      // pushq %rsi
      emit_bytes(0x56);
      if constexpr (sizeof(U) == 4) {
         // movq 16(%rsp), %edi
         emit_bytes(0x8b, 0x7c, 0x24, 0x10 + extra);
      } else {
         // movq 16(%rsp), %rdi
         emit_bytes(0x48, 0x8b, 0x7c, 0x24, 0x10 + extra);
      }
      emit_align_stack();
      // movabsq $softfloatfun, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand_ptr(softfloatfun);
      // callq *%rax
      emit_bytes(0xff, 0xd0);
      emit_restore_stack();
      // popq %rsi
      emit_bytes(0x5e);
      // popq %rdi
      emit_bytes(0x5f);
      emit_restore_backtrace();
      if constexpr (sizeof(T) == 4) {
         static_assert(sizeof(U) == 4, "Can only push 4-byte item if the upper 4 bytes are already 0");
         // movq %eax, (%rsp)
         emit_bytes(0x89, 0x04, 0x24);
      } else {
         // movq %rax, (%rsp)
         emit_bytes(0x48, 0x89, 0x04, 0x24);
      }
   }

   void emit_f32_binop_softfloat(float32_t (*softfloatfun)(float32_t, float32_t)) {
      auto extra = emit_setup_backtrace();
      // pushq %rdi
      emit_bytes(0x57);
      // pushq %rsi
      emit_bytes(0x56);
      // movq 16(%rsp), %esi
      emit_bytes(0x8b, 0x74, 0x24, 0x10 + extra);
      // movq 24(%rsp), %edi
      emit_bytes(0x8b, 0x7c, 0x24, 0x18 + extra);
      emit_align_stack();
      // movabsq $softfloatfun, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand_ptr(softfloatfun);
      // callq *%rax
      emit_bytes(0xff, 0xd0);
      emit_restore_stack();
      // popq %rsi
      emit_bytes(0x5e);
      // popq %rdi
      emit_bytes(0x5f);
      emit_restore_backtrace_basic();
      // addq $8, %rsp
      emit_bytes(0x48, 0x83, 0xc4, 0x08 + extra);
      // movq %eax, (%rsp)
      emit_bytes(0x89, 0x04, 0x24);
   }

   void emit_f64_binop_softfloat(float64_t (*softfloatfun)(float64_t, float64_t)) {
      auto extra = emit_setup_backtrace();
      // pushq %rdi
      emit_bytes(0x57);
      // pushq %rsi
      emit_bytes(0x56);
      // movq 16(%rsp), %rsi
      emit_bytes(0x48, 0x8b, 0x74, 0x24, 0x10 + extra);
      // movq 24(%rsp), %rdi
      emit_bytes(0x48, 0x8b, 0x7c, 0x24, 0x18 + extra);
      emit_align_stack();
      // movabsq $softfloatfun, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand_ptr(softfloatfun);
      // callq *%rax
      emit_bytes(0xff, 0xd0);
      emit_restore_stack();
      // popq %rsi
      emit_bytes(0x5e);
      // popq %rdi
      emit_bytes(0x5f);
      emit_restore_backtrace_basic();
      // addq $8, %rsp
      emit_bytes(0x48, 0x83, 0xc4, 0x08 + extra);
      // movq %rax, (%rsp)
      emit_bytes(0x48, 0x89, 0x04, 0x24);
   }

   void emit_f32_relop(uint8_t opcode, uint64_t (*softfloatfun)(float32_t, float32_t), bool switch_params,
                       bool flip_result) {
      if constexpr (use_softfloat) {
         auto extra = emit_setup_backtrace();
         // pushq %rdi
         emit_bytes(0x57);
         // pushq %rsi
         emit_bytes(0x56);
         if (switch_params) {
            // movq 24(%rsp), %esi
            emit_bytes(0x8b, 0x74, 0x24, 0x18 + extra);
            // movq 16(%rsp), %edi
            emit_bytes(0x8b, 0x7c, 0x24, 0x10 + extra);
         } else {
            // movq 16(%rsp), %esi
            emit_bytes(0x8b, 0x74, 0x24, 0x10 + extra);
            // movq 24(%rsp), %edi
            emit_bytes(0x8b, 0x7c, 0x24, 0x18 + extra);
         }
         emit_align_stack();
         // movabsq $softfloatfun, %rax
         emit_bytes(0x48, 0xb8);
         emit_operand_ptr(softfloatfun);
         // callq *%rax
         emit_bytes(0xff, 0xd0);
         emit_restore_stack();
         // popq %rsi
         emit_bytes(0x5e);
         // popq %rdi
         emit_bytes(0x5f);
         emit_restore_backtrace_basic();
         if (flip_result) {
            // xor $0x1, %al
            emit_bytes(0x34, 0x01);
         }
         // addq $8, %rsp
         emit_bytes(0x48, 0x83, 0xc4, 0x08 + extra);
         // movq %rax, (%rsp)
         emit_bytes(0x48, 0x89, 0x04, 0x24);
      } else {
         // ucomiss+seta/setae is shorter but can't handle eq/ne
         if (switch_params) {
            // movss (%rsp), %xmm0
            emit_bytes(0xf3, 0x0f, 0x10, 0x04, 0x24);
            // cmpCCss 8(%rsp), %xmm0
            emit_bytes(0xf3, 0x0f, 0xc2, 0x44, 0x24, 0x08, opcode);
         } else {
            // movss 8(%rsp), %xmm0
            emit_bytes(0xf3, 0x0f, 0x10, 0x44, 0x24, 0x08);
            // cmpCCss (%rsp), %xmm0
            emit_bytes(0xf3, 0x0f, 0xc2, 0x04, 0x24, opcode);
         }
         // movd %xmm0, %eax
         emit_bytes(0x66, 0x0f, 0x7e, 0xc0);
         if (!flip_result) {
            // andl $1, %eax
            emit_bytes(0x83, 0xe0, 0x01);
         } else {
            // incl %eax {0xffffffff, 0} -> {0, 1}
            emit_bytes(0xff, 0xc0);
         }
         // leaq 16(%rsp), %rsp
         emit_bytes(0x48, 0x8d, 0x64, 0x24, 0x10);
         // pushq %rax
         emit_bytes(0x50);
      }
   }

   void emit_f64_relop(uint8_t opcode, uint64_t (*softfloatfun)(float64_t, float64_t), bool switch_params,
                       bool flip_result) {
      if constexpr (use_softfloat) {
         auto extra = emit_setup_backtrace();
         // pushq %rdi
         emit_bytes(0x57);
         // pushq %rsi
         emit_bytes(0x56);
         if (switch_params) {
            // movq 24(%rsp), %rsi
            emit_bytes(0x48, 0x8b, 0x74, 0x24, 0x18 + extra);
            // movq 16(%rsp), %rdi
            emit_bytes(0x48, 0x8b, 0x7c, 0x24, 0x10 + extra);
         } else {
            // movq 16(%rsp), %rsi
            emit_bytes(0x48, 0x8b, 0x74, 0x24, 0x10 + extra);
            // movq 24(%rsp), %rdi
            emit_bytes(0x48, 0x8b, 0x7c, 0x24, 0x18 + extra);
         }
         emit_align_stack();
         // movabsq $softfloatfun, %rax
         emit_bytes(0x48, 0xb8);
         emit_operand_ptr(softfloatfun);
         // callq *%rax
         emit_bytes(0xff, 0xd0);
         emit_restore_stack();
         // popq %rsi
         emit_bytes(0x5e);
         // popq %rdi
         emit_bytes(0x5f);
         emit_restore_backtrace_basic();
         if (flip_result) {
            // xor $0x1, %al
            emit_bytes(0x34, 0x01);
         }
         // addq $8, %rsp
         emit_bytes(0x48, 0x83, 0xc4, 0x08 + extra);
         // movq %rax, (%rsp)
         emit_bytes(0x48, 0x89, 0x04, 0x24);
      } else {
         // ucomisd+seta/setae is shorter but can't handle eq/ne
         if (switch_params) {
            // movsd (%rsp), %xmm0
            emit_bytes(0xf2, 0x0f, 0x10, 0x04, 0x24);
            // cmpCCsd 8(%rsp), %xmm0
            emit_bytes(0xf2, 0x0f, 0xc2, 0x44, 0x24, 0x08, opcode);
         } else {
            // movsd 8(%rsp), %xmm0
            emit_bytes(0xf2, 0x0f, 0x10, 0x44, 0x24, 0x08);
            // cmpCCsd (%rsp), %xmm0
            emit_bytes(0xf2, 0x0f, 0xc2, 0x04, 0x24, opcode);
         }
         // movd %xmm0, %eax
         emit_bytes(0x66, 0x0f, 0x7e, 0xc0);
         if (!flip_result) {
            // andl $1, eax
            emit_bytes(0x83, 0xe0, 0x01);
         } else {
            // incl %eax {0xffffffff, 0} -> {0, 1}
            emit_bytes(0xff, 0xc0);
         }
         // leaq 16(%rsp), %rsp
         emit_bytes(0x48, 0x8d, 0x64, 0x24, 0x10);
         // pushq %rax
         emit_bytes(0x50);
      }
   }

   template <class... T> void emit_i32_binop(T... op) {
      // popq %rcx
      emit_bytes(0x59);
      // popq %rax
      emit_bytes(0x58);
      // OP %eax, %ecx
      emit_bytes(static_cast<uint8_t>(op)...);
      // pushq %rax
      // emit_bytes(0x50);
   }

   template <class... T> void emit_i64_binop(T... op) {
      // popq %rcx
      emit_bytes(0x59);
      // popq %rax
      emit_bytes(0x58);
      // OP %eax, %ecx
      emit_bytes(static_cast<uint8_t>(op)...);
   }

   void emit_f32_binop(uint8_t op, float32_t (*softfloatfun)(float32_t, float32_t)) {
      if constexpr (use_softfloat) {
         return emit_f32_binop_softfloat(softfloatfun);
      }
      // movss 8(%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, 0x10, 0x44, 0x24, 0x08);
      // OPss (%rsp), %xmm0
      emit_bytes(0xf3, 0x0f, op, 0x04, 0x24);
      // leaq 8(%rsp), %rsp
      emit_bytes(0x48, 0x8d, 0x64, 0x24, 0x08);
      // movss %xmm0, (%rsp)
      emit_bytes(0xf3, 0x0f, 0x11, 0x04, 0x24);
   }

   void emit_f64_binop(uint8_t op, float64_t (*softfloatfun)(float64_t, float64_t)) {
      if constexpr (use_softfloat) {
         return emit_f64_binop_softfloat(softfloatfun);
      }
      // movsd 8(%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, 0x10, 0x44, 0x24, 0x08);
      // OPsd (%rsp), %xmm0
      emit_bytes(0xf2, 0x0f, op, 0x04, 0x24);
      // leaq 8(%rsp), %rsp
      emit_bytes(0x48, 0x8d, 0x64, 0x24, 0x08);
      // movsd %xmm0, (%rsp)
      emit_bytes(0xf2, 0x0f, 0x11, 0x04, 0x24);
   }

   // Beware: This pushes and pops mxcsr around the user op.  Remember to adjust access to %rsp in the caller.
   // Note uses %rcx after the user instruction
   template <class... T> void emit_f2i(T... op) {
      // mov 0x0x1f80, %eax // round-to-even/all exceptions masked/no exceptions set
      emit_bytes(0xb8, 0x80, 0x1f, 0x00, 0x00);
      // push %rax
      emit_bytes(0x50);
      // ldmxcsr (%rsp)
      emit_bytes(0x0f, 0xae, 0x14, 0x24);
      // user op
      emit_bytes(op...);
      // stmxcsr (%rsp)
      emit_bytes(0x0f, 0xae, 0x1c, 0x24);
      // pop %rcx
      emit_bytes(0x59);
      // test %cl, 0x1 // invalid
      emit_bytes(0xf6, 0xc1, 0x01);
      // jnz FP_ERROR_HANDLER
      emit_bytes(0x0f, 0x85);
      fix_branch(emit_branch_target32(), fpe_handler);
   }

   void* emit_error_handler(void (*handler)()) {
      void* result = code;
      // andq $-16, %rsp;
      emit_bytes(0x48, 0x83, 0xe4, 0xf0);
      // movabsq &on_unreachable, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand_ptr(handler);
      // callq *%rax
      emit_bytes(0xff, 0xd0);
      return result;
   }

   void emit_align_stack() {
      // mov %rsp, rcx; andq $-16, %rsp; push rcx; push %rcx
      emit_bytes(0x48, 0x89, 0xe1);
      emit_bytes(0x48, 0x83, 0xe4, 0xf0);
      emit_bytes(0x51);
      emit_bytes(0x51);
   }

   void emit_restore_stack() {
      // mov (%rsp), %rsp
      emit_bytes(0x48, 0x8b, 0x24, 0x24);
   }

   void emit_host_call(uint32_t funcnum) {
      uint32_t extra = 0;
      if constexpr (Context::async_backtrace()) {
         // pushq %rbp
         emit_bytes(0x55);
         // movq %rsp, (%rdi)
         emit_bytes(0x48, 0x89, 0x27);
         extra = 8;
      }
      // mov $funcnum, %edx
      emit_bytes(0xba);
      emit_operand32(funcnum);
      // pushq %rdi
      emit_bytes(0x57);
      // pushq %rsi
      emit_bytes(0x56);
      // lea 24(%rsp), %rsi
      emit_bytes(0x48, 0x8d, 0x74, 0x24, 0x18 + extra);
      emit_align_stack();
      // movabsq $call_host_function, %rax
      emit_bytes(0x48, 0xb8);
      emit_operand_ptr(&call_host_function);
      // callq *%rax
      emit_bytes(0xff, 0xd0);
      emit_restore_stack();
      // popq %rsi
      emit_bytes(0x5e);
      // popq %rdi
      emit_bytes(0x5f);
      if constexpr (Context::async_backtrace()) {
         emit_restore_backtrace_basic();
         // popq %rbp
         emit_bytes(0x5d);
      }
      // retq
      emit_bytes(0xc3);
   }

   // Needs to run before saving %rdi.  Returns the number of bytes pushed onto the stack.
   uint32_t emit_setup_backtrace() {
      if constexpr (Context::async_backtrace()) {
         // callq next
         emit_bytes(0xe8);
         emit_operand32(0);
         // next:
         // pushq %rbp
         emit_bytes(0x55);
         // movq %rsp, (%rdi)
         emit_bytes(0x48, 0x89, 0x27);
         return 16;
      } else {
         return 0;
      }
   }
   // Does not adjust the stack pointer.  Use this if the
   // stack pointer adjustment is combined with another instruction.
   void emit_restore_backtrace_basic() {
      if constexpr (Context::async_backtrace()) {
         // xorl %edx, %edx
         emit_bytes(0x31, 0xd2);
         // movq %rdx, (%rdi)
         emit_bytes(0x48, 0x89, 0x17);
      }
   }
   void emit_restore_backtrace() {
      if constexpr (Context::async_backtrace()) {
         emit_restore_backtrace_basic();
         // addq $16, %rsp
         emit_bytes(0x48, 0x83, 0xc4, 0x10);
      }
   }

   bool is_host_function(uint32_t funcnum) {
      return funcnum < _mod.get_imported_functions_size();
   }

   static native_value call_host_function(Context* context /*rdi*/, native_value* stack /*rsi*/, uint32_t idx /*edx*/) {
      // It's currently unsafe to throw through a jit frame, because we don't set up
      // the exception tables for them.
      native_value result;
      ::forge::vm::wasm::longjmp_on_exception([&]() { result = context->call_host_function(stack, idx); });
      return result;
   }

   static int32_t current_memory(Context* context /*rdi*/) {
      return context->current_linear_memory();
   }

   static int32_t grow_memory(Context* context /*rdi*/, int32_t pages) {
      return context->grow_linear_memory(pages);
   }

   static int32_t get_global_i32(Context* context /*rdi*/, uint32_t index /*rsi*/) {
      return context->get_global_i32(index);
   }
   static int64_t get_global_i64(Context* context /*rdi*/, uint32_t index /*rsi*/) {
      return context->get_global_i64(index);
   }
   static uint32_t get_global_f32(Context* context /*rdi*/, uint32_t index /*rsi*/) {
      return context->get_global_f32(index);
   }
   static uint64_t get_global_f64(Context* context /*rdi*/, uint32_t index /*rsi*/) {
      return context->get_global_f64(index);
   }

   static void set_global_i32(Context* context /*rdi*/, uint32_t index /*rsi*/, int32_t value /*rdx*/) {
      context->set_global_i32(index, value);
   }
   static void set_global_i64(Context* context /*rdi*/, uint32_t index /*rsi*/, int64_t value /*rdx*/) {
      context->set_global_i64(index, value);
   }
   static void set_global_f32(Context* context /*rdi*/, uint32_t index /*rsi*/, uint32_t value /*rdx*/) {
      context->set_global_f32(index, value);
   }
   static void set_global_f64(Context* context /*rdi*/, uint32_t index /*rsi*/, uint64_t value /*rdx*/) {
      context->set_global_f64(index, value);
   }

   static void on_unreachable() {
      ::forge::vm::wasm::throw_<exceptions::interpreter>("unreachable");
   }
   static void on_fp_error() {
      ::forge::vm::wasm::throw_<exceptions::interpreter>("floating point error");
   }
   static void on_call_indirect_error() {
      ::forge::vm::wasm::throw_<exceptions::interpreter>("call_indirect out of range");
   }
   static void on_type_error() {
      ::forge::vm::wasm::throw_<exceptions::interpreter>("call_indirect incorrect function type");
   }
   static void on_stack_overflow() {
      ::forge::vm::wasm::throw_<exceptions::interpreter>("stack overflow");
   }
};

} // namespace forge::vm::wasm
#endif

export namespace forge::vm::wasm {

#ifdef __x86_64__
struct jit {
   template <typename Host> using context = jit_execution_context<Host>;
   template <typename Host, typename Options, typename DebugInfo>
   using parser = binary_parser<machine_code_writer<jit_execution_context<Host>>, Options, DebugInfo>;
   static constexpr bool is_jit = true;
};

struct jit_profile {
   template <typename Host> using context = jit_execution_context<Host, true>;
   template <typename Host, typename Options, typename DebugInfo>
   using parser = binary_parser<machine_code_writer<context<Host>>, Options, DebugInfo>;
   static constexpr bool is_jit = true;
};
#endif

struct interpreter {
   template <typename Host> using context = execution_context<Host>;
   template <typename Host, typename Options, typename DebugInfo>
   using parser = binary_parser<bitcode_writer, Options, DebugInfo>;
   static constexpr bool is_jit = false;
};

struct null_backend {
   template <typename Host> using context = null_execution_context<Host>;
   template <typename Host, typename Options, typename DebugInfo>
   using parser = binary_parser<null_writer, Options, DebugInfo>;
   static constexpr bool is_jit = false;
};

template <typename T> struct maybe_unique_ptr {
   maybe_unique_ptr(T* ptr = nullptr, bool owns = true) : ptr(ptr), owns(owns) {}
   maybe_unique_ptr(const maybe_unique_ptr&) = delete;
   maybe_unique_ptr& operator=(const maybe_unique_ptr&) = delete;
   ~maybe_unique_ptr() {
      if (ptr && owns)
         delete ptr;
   }
   T& operator*() const {
      return *ptr;
   }
   T* operator->() const {
      return ptr;
   }
   T* get() const {
      return ptr;
   }
   void reset(T* new_ptr, bool new_owns = true) {
      if (ptr && owns)
         delete ptr;
      this->ptr = new_ptr;
      this->owns = new_owns;
   }

 private:
   T* ptr;

 public:
   bool owns;
};

template <typename HostFunctions = std::nullptr_t, typename Impl = interpreter, typename Options = default_options,
          typename DebugInfo = null_debug_info>
class backend {
   using host_t = detail::host_type_t<HostFunctions>;
   using context_t = typename Impl::template context<HostFunctions>;
   using parser_t = typename Impl::template parser<HostFunctions, Options, DebugInfo>;
   void construct(host_t* host = nullptr) {
      mod->finalize();
      if (ctx.owns) {
         ctx->set_wasm_allocator(memory_alloc);
      }
      // Now data required by JIT is finalized; create JIT module
      // such that memory used in parsing can be released.
      if constexpr (Impl::is_jit) {
         mod->make_jit_module();

         // Important. Release the memory used by parsing.
         mod->allocator.release_base_memory();
      }
      if (ctx.owns) {
         ctx->initialize_globals();
      }
      if constexpr (!std::is_same_v<HostFunctions, std::nullptr_t>)
         HostFunctions::resolve(*mod);
      // FIXME: should not hard code knowledge of null_backend here
      if (ctx.owns) {
         if constexpr (!std::is_same_v<Impl, null_backend>)
            initialize(host);
      }
   }

 public:
   backend() {}
   backend(wasm_code&& code, host_t& host, wasm_allocator* alloc, const Options& options = Options{})
       : memory_alloc(alloc), mod(std::make_shared<module>()),
         ctx(new context_t{parse_module(code, options), detail::get_max_call_depth(options)}), mod_sharable{true} {
      ctx->set_max_pages(detail::get_max_pages(options));
      construct(&host);
   }
   backend(wasm_code&& code, wasm_allocator* alloc, const Options& options = Options{})
       : memory_alloc(alloc), mod(std::make_shared<module>()),
         ctx(new context_t{parse_module(code, options), detail::get_max_call_depth(options)}), mod_sharable{true} {
      ctx->set_max_pages(detail::get_max_pages(options));
      construct();
   }
   backend(wasm_code& code, host_t& host, wasm_allocator* alloc, const Options& options = Options{})
       : memory_alloc(alloc), mod(std::make_shared<module>()),
         ctx(new context_t{parse_module(code, options), detail::get_max_call_depth(options)}), mod_sharable{true} {
      ctx->set_max_pages(detail::get_max_pages(options));
      construct(&host);
   }
   backend(wasm_code& code, wasm_allocator* alloc, const Options& options = Options{})
       : memory_alloc(alloc), mod(std::make_shared<module>()),
         ctx(new context_t{(parse_module(code, options)), detail::get_max_call_depth(options)}), mod_sharable{true} {
      ctx->set_max_pages(detail::get_max_pages(options));
      construct();
   }
   backend(wasm_code_ptr& ptr, size_t sz, host_t& host, wasm_allocator* alloc, const Options& options = Options{})
       : memory_alloc(alloc), mod(std::make_shared<module>()),
         ctx(new context_t{parse_module2(ptr, sz, options, true), detail::get_max_call_depth(options)}),
         mod_sharable{true} { // single parsing. original behavior {
      ctx->set_max_pages(detail::get_max_pages(options));
      construct(&host);
   }
   // Leap:
   //  * Contract validation only needs single parsing as the instantiated module is not cached.
   //  * JIT execution needs single parsing only.
   //  * Interpreter execution requires two-passes parsing to prevent memory mappings exhaustion
   //  * Leap reuses execution context per thread; ctx.owns is set
   //  to false when a backend is constructued
   backend(wasm_code_ptr& ptr, size_t sz, wasm_allocator* alloc, const Options& options = Options{},
           bool single_parsing = true, bool exec_ctx_by_backend = true)
       : memory_alloc(alloc), mod(std::make_shared<module>()), ctx(nullptr, exec_ctx_by_backend), mod_sharable{true},
         initial_max_call_depth(detail::get_max_call_depth(options)),
         initial_max_pages(detail::get_max_pages(options)) {
      if (ctx.owns) {
         ctx.reset(new context_t{parse_module2(ptr, sz, options, single_parsing), initial_max_call_depth});
         ctx->set_max_pages(initial_max_pages);
      } else {
         parse_module2(ptr, sz, options, single_parsing);
      }
      construct();
   }

   module& parse_module(wasm_code& code, const Options& options) {
      mod->allocator.use_default_memory();
      return parser_t{mod->allocator, options}.parse_module(code, *mod, debug);
   }

   module& parse_module2(wasm_code_ptr& ptr, size_t sz, const Options& options, bool single_parsing) {
      if (single_parsing) {
         mod->allocator.use_default_memory();
         return parser_t{mod->allocator, options}.parse_module2(ptr, sz, *mod, debug);
      } else {
         // To prevent large number of memory mappings used, two-passes of
         // parsing are performed.
         wasm_code_ptr orig_ptr = ptr;
         size_t largest_size = 0;

         // First pass: finds max size of memory required by parsing.
         {
            // Memory used by this pass is freed when going out of the scope
            ::forge::vm::wasm::module first_pass_module;
            first_pass_module.allocator.use_default_memory();
            parser_t{first_pass_module.allocator, options}.parse_module2(ptr, sz, first_pass_module, debug);
            first_pass_module.finalize();
            largest_size = first_pass_module.allocator.largest_used_size();
         }

         // Second pass: uses actual required memory for final parsing
         mod->allocator.use_fixed_memory(largest_size);
         return parser_t{mod->allocator, options}.parse_module2(orig_ptr, sz, *mod, debug);
      }
   }

   // Shares compiled module with another backend which never compiles
   // module itself.
   void share(const backend& from) {
      assert(from.mod_sharable); // `from` backend's mod is sharable
      assert(!mod_sharable);     // `to` backend's mod must not be sharable
      mod = from.mod;
      ctx.owns = from.ctx.owns;
      initial_max_call_depth = from.initial_max_call_depth;
      initial_max_pages = from.initial_max_pages;
   }

   void set_context(context_t* ctx_ptr) {
      // ctx cannot be set if it is created by the backend
      assert(!ctx.owns);
      ctx.reset(ctx_ptr, false);
   }

   inline void reset_max_call_depth() {
      // max_call_depth cannot be reset if ctx is created by the backend
      assert(!ctx.owns);
      ctx->set_max_call_depth(initial_max_call_depth);
   }

   inline void reset_max_pages() {
      // max_pages cannot be reset if ctx is created by the backend
      assert(!ctx.owns);
      ctx->set_max_pages(initial_max_pages);
   }

   template <typename... Args>
   inline auto operator()(host_t& host, const std::string_view& mod, const std::string_view& func, Args... args) {
      return call(host, mod, func, args...);
   }

   template <typename... Args>
   inline bool operator()(const std::string_view& mod, const std::string_view& func, Args... args) {
      return call(mod, func, args...);
   }

   // Only dynamic options matter.  Parser options will be ignored.
   inline backend& initialize(host_t* host, const Options& new_options) {
      ctx->set_max_call_depth(detail::get_max_call_depth(new_options));
      ctx->set_max_pages(detail::get_max_pages(new_options));
      initialize(host);
      return *this;
   }

   inline backend& initialize(host_t* host = nullptr) {
      if (memory_alloc) {
         ctx->reset();
         ctx->execute_start(host, interpret_visitor(*ctx));
      }
      return *this;
   }

   inline backend& initialize(host_t& host) {
      return initialize(&host);
   }

   template <typename... Args> inline bool call_indirect(host_t* host, uint32_t func_index, Args&&... args) {
      if constexpr (wasm_debug) {
         ctx->execute_func_table(host, debug_visitor(*ctx), func_index, std::forward<Args>(args)...);
      } else {
         ctx->execute_func_table(host, interpret_visitor(*ctx), func_index, std::forward<Args>(args)...);
      }
      return true;
   }

   template <typename... Args> inline bool call(host_t* host, uint32_t func_index, Args&&... args) {
      if constexpr (wasm_debug) {
         ctx->execute(host, debug_visitor(*ctx), func_index, std::forward<Args>(args)...);
      } else {
         ctx->execute(host, interpret_visitor(*ctx), func_index, std::forward<Args>(args)...);
      }
      return true;
   }

   template <typename... Args>
   inline bool call(host_t& host, const std::string_view& mod, const std::string_view& func, Args&&... args) {
      if constexpr (wasm_debug) {
         ctx->execute(&host, debug_visitor(*ctx), func, std::forward<Args>(args)...);
      } else {
         ctx->execute(&host, interpret_visitor(*ctx), func, std::forward<Args>(args)...);
      }
      return true;
   }

   template <typename... Args>
   inline bool call(const std::string_view& mod, const std::string_view& func, Args&&... args) {
      if constexpr (wasm_debug) {
         ctx->execute(nullptr, debug_visitor(*ctx), func, std::forward<Args>(args)...);
      } else {
         ctx->execute(nullptr, interpret_visitor(*ctx), func, std::forward<Args>(args)...);
      }
      return true;
   }

   template <typename... Args>
   inline auto call_with_return(host_t& host, const std::string_view& mod, const std::string_view& func,
                                Args&&... args) {
      if constexpr (wasm_debug) {
         return ctx->execute(&host, debug_visitor(*ctx), func, std::forward<Args>(args)...);
      } else {
         return ctx->execute(&host, interpret_visitor(*ctx), func, std::forward<Args>(args)...);
      }
   }

   template <typename... Args>
   inline auto call_with_return(const std::string_view& mod, const std::string_view& func, Args&&... args) {
      if constexpr (wasm_debug) {
         return ctx->execute(nullptr, debug_visitor(*ctx), func, std::forward<Args>(args)...);
      } else {
         return ctx->execute(nullptr, interpret_visitor(*ctx), func, std::forward<Args>(args)...);
      }
   }

   template <typename Watchdog, typename F> inline void timed_run(Watchdog&& wd, F&& f) {
      // timed_run_has_timed_out -- declared in signal handling code because signal handler needs to inspect it on a
      // SEGV too -- is a thread local
      //  so that upon a SEGV the signal handling code can discern if the thread that caused the SEGV has a timed_run
      //  that has timed out. This thread local also need to be an atomic because the thread that a Watchdog callback
      //  will be called from may not be the same as the executing thread.
      std::atomic<bool>& _timed_out = timed_run_has_timed_out;
      auto reenable_code = scope_guard{[&]() {
         if (_timed_out.load(std::memory_order_acquire)) {
            mod->allocator.enable_code(Impl::is_jit);
            _timed_out.store(false, std::memory_order_release);
         }
      }};
      try {
         auto wd_guard = std::forward<Watchdog>(wd).scoped_run([this, &_timed_out]() {
            _timed_out.store(true, std::memory_order_release);
            mod->allocator.disable_code();
         });
         std::forward<F>(f)();
      } catch (exceptions::memory&) {
         if (_timed_out.load(std::memory_order_acquire)) {
            throw exceptions::timeout{"execution timed out"};
         } else {
            throw;
         }
      }
   }

   template <typename Watchdog> inline void execute_all(Watchdog&& wd, host_t& host) {
      timed_run(std::forward<Watchdog>(wd), [&]() {
         for (int i = 0; i < mod->exports.size(); i++) {
            if (mod->exports[i].kind == external_kind::Function) {
               std::string s{(const char*)mod->exports[i].field_str.raw(), mod->exports[i].field_str.size()};
               ctx->execute(host, interpret_visitor(*ctx), s);
            }
         }
      });
   }

   template <typename Watchdog> inline void execute_all(Watchdog&& wd) {
      timed_run(std::forward<Watchdog>(wd), [&]() {
         for (int i = 0; i < mod->exports.size(); i++) {
            if (mod->exports[i].kind == external_kind::Function) {
               std::string s{(const char*)mod->exports[i].field_str.raw(), mod->exports[i].field_str.size()};
               ctx->execute(nullptr, interpret_visitor(*ctx), s);
            }
         }
      });
   }

   inline void set_wasm_allocator(wasm_allocator* alloc) {
      memory_alloc = alloc;
      ctx->set_wasm_allocator(memory_alloc);
   }

   inline module& get_module() {
      return *mod;
   }
   inline void exit(const std::error_code& ec) {
      ctx->exit(ec);
   }
   inline auto& get_context() {
      return *ctx;
   }

   const DebugInfo& get_debug() const {
      return debug;
   }

 private:
   wasm_allocator* memory_alloc = nullptr; // non owning pointer
   std::shared_ptr<module> mod = nullptr;
   DebugInfo debug;
   maybe_unique_ptr<context_t> ctx = nullptr;
   bool mod_sharable = false; // true if mod is sharable (compiled by the backend)
   uint32_t initial_max_call_depth = 0;
   uint32_t initial_max_pages = 0;
};
} // namespace forge::vm::wasm
