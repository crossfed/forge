module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <fstream>
#include <forge/vm/wasm/opcode_macros.hpp>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module forge.vm.wasm.types;

export import forge.vm.wasm.allocator;
export import forge.vm.wasm.exceptions;

namespace forge::vm::wasm::detail {
template <typename Exception, typename Message>
[[noreturn]] inline void fail(Message&& message,
                              std::source_location location = std::source_location::current()) {
   throw Exception{std::string{std::forward<Message>(message)}, {}, location};
}

template <typename Exception, typename Message>
inline void check(bool expression, Message&& message,
                  std::source_location location = std::source_location::current()) {
   if (!expression) [[unlikely]] {
      fail<Exception>(std::forward<Message>(message), location);
   }
}
} // namespace forge::vm::wasm::detail

// temporarily use exceptions


export namespace forge::vm::wasm {

   // forward declaration
   template <typename... Alternatives>
   class variant;

   // implementation details
   namespace detail {

      template <typename... Ts>
      constexpr std::size_t max_layout_size_v = std::max({sizeof(Ts)...});

      template <typename... Ts>
      constexpr std::size_t max_alignof_v = std::max({alignof(Ts)...});

      template <typename T, typename... Alternatives>
      constexpr bool is_valid_alternative_v = (... + (std::is_same_v<T, Alternatives>?1:0)) != 0;

      template <typename T, typename Alternative, typename... Alternatives>
      constexpr std::size_t get_alternatives_index_v =
               std::is_same_v<T, Alternative> ? 0 : get_alternatives_index_v<T, Alternatives...> + 1;

      template <typename T, typename Alternative>
      constexpr std::size_t get_alternatives_index_v<T, Alternative> = 0;

      template <std::size_t I, typename... Alternatives>
      using get_alternative_t = std::tuple_element_t<I, std::tuple<Alternatives...>>;

      template <bool Valid, typename Ret>
      struct dispatcher;

      template <typename Ret>
      struct dispatcher<false, Ret> {
         template <std::size_t I, typename Vis, typename Var>
         static constexpr Ret _case(Vis&&, Var&&) {
            throw exceptions::interpreter("variant visit shouldn't be here");
         }
         template <std::size_t I, typename Vis, typename Var>
         static constexpr Ret _switch(Vis&&, Var&&) {
            throw exceptions::interpreter("variant visit shouldn't be here");
         }
      };

      template <typename Ret>
      struct dispatcher<true, Ret> {
         template <std::size_t I, typename Vis, typename Var>
         static constexpr Ret _case(Vis&& vis, Var&& var) {
            return std::invoke(std::forward<Vis>(vis), std::forward<Var>(var).template get<I>());
         }

         template <std::size_t I, typename Vis, typename Var>
         static constexpr Ret _switch(Vis&& vis, Var&& var) {
            constexpr std::size_t sz = std::decay_t<Var>::variant_size();
            switch (var.index()) {
               case I + 0: {
                  return dispatcher<I + 0 < sz, Ret>::template _case<I + 0>(std::forward<Vis>(vis),
                                                                            std::forward<Var>(var));
               }
               case I + 1: {
                  return dispatcher<I + 1 < sz, Ret>::template _case<I + 1>(std::forward<Vis>(vis),
                                                                            std::forward<Var>(var));
               }
               case I + 2: {
                  return dispatcher<I + 2 < sz, Ret>::template _case<I + 2>(std::forward<Vis>(vis),
                                                                            std::forward<Var>(var));
               }
               case I + 3: {
                  return dispatcher<I + 3 < sz, Ret>::template _case<I + 3>(std::forward<Vis>(vis),
                                                                            std::forward<Var>(var));
               }
               default: {
                  return dispatcher<I + 4 < sz, Ret>::template _switch<I + 4>(std::forward<Vis>(vis),
                                                                              std::forward<Var>(var));
               }
            }
         }
      };

#define V_ELEM(N)                                                       \
      T##N _t##N;                                                       \
      constexpr variant_storage(T##N& arg) : _t##N(arg) {}              \
      constexpr variant_storage(T##N&& arg) : _t##N(std::move(arg)) {}  \
      constexpr variant_storage(const T##N& arg) : _t##N(arg) {}        \
      constexpr variant_storage(const T##N&& arg) : _t##N(std::move(arg)) {}

#define V0 variant_storage() = default;
#define V1 V0 V_ELEM(0)
#define V2 V1 V_ELEM(1)
#define V3 V2 V_ELEM(2)
#define V4 V3 V_ELEM(3)

      template<typename... T>
      union variant_storage;
      template<typename T0, typename T1, typename T2, typename T3, typename... T>
      union variant_storage<T0, T1, T2, T3, T...> {
         V4
         template<typename A>
         constexpr variant_storage(A&& arg) : _tail{std::forward<A>(arg)} {}
         variant_storage<T...> _tail;
      };
      template<typename T0>
      union variant_storage<T0> {
         V1
      };
      template<typename T0, typename T1>
      union variant_storage<T0, T1> {
         V2
      };
      template<typename T0, typename T1, typename T2>
      union variant_storage<T0, T1, T2> {
         V3
      };
      template<typename T0, typename T1, typename T2, typename T3>
      union variant_storage<T0, T1, T2, T3> {
         V4
      };

#undef V4
#undef V3
#undef V2
#undef V1
#undef V0
#undef V_ELEM

      template<int I, typename Storage>
      constexpr decltype(auto) variant_storage_get(Storage&& val) {
         if constexpr (I == 0) {
            return (std::forward<Storage>(val)._t0);
         } else if constexpr (I == 1) {
            return (std::forward<Storage>(val)._t1);
         } else if constexpr (I == 2) {
            return (std::forward<Storage>(val)._t2);
         } else if constexpr (I == 3) {
            return (std::forward<Storage>(val)._t3);
         } else {
            return detail::variant_storage_get<I - 4>(std::forward<Storage>(val)._tail);
         }
      }
   } // namespace detail

   template <class Visitor, typename Variant>
   constexpr auto visit(Visitor&& vis, Variant&& var) {
      using Ret = decltype(std::invoke(std::forward<Visitor>(vis), var.template get<0>()));
      return detail::dispatcher<true, Ret>::template _switch<0>(std::forward<Visitor>(vis), std::forward<Variant>(var));
   }

   template <typename... Alternatives>
   class variant {
      static_assert(sizeof...(Alternatives) <= std::numeric_limits<uint8_t>::max()+1,
                    "forge::vm::wasm::variant can only accept 256 alternatives");
      static_assert((... && (std::is_trivially_copy_constructible_v<Alternatives> && std::is_trivially_move_constructible_v<Alternatives> &&
                    std::is_trivially_copy_assignable_v<Alternatives> && std::is_trivially_move_assignable_v<Alternatives> &&
                    std::is_trivially_destructible_v<Alternatives>)), "Variant requires trivial types");

    public:
      variant() = default;
      variant(const variant& other) = default;
      variant(variant&& other) = default;

      variant& operator=(const variant& other) = default;
      variant& operator=(variant&& other) = default;

      template <typename T, typename = std::enable_if_t<detail::is_valid_alternative_v<std::decay_t<T>, Alternatives...>>>
      constexpr variant(T&& alt) :
         _which(detail::get_alternatives_index_v<std::decay_t<T>, Alternatives...>),
         _storage(std::forward<T>(alt)) {
      }

      template <typename T,
                typename = std::enable_if_t<detail::is_valid_alternative_v<std::decay_t<T>, Alternatives...>>>
      constexpr variant& operator=(T&& alt) {
#if (defined(__GNUC__) && !defined(__clang__))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
         _storage = std::forward<T>(alt);
#pragma GCC diagnostic pop
#else
        _storage = std::forward<T>(alt);
#endif
         _which = detail::get_alternatives_index_v<std::decay_t<T>, Alternatives...>;
         return *this;
      }

      static inline constexpr size_t variant_size() { return sizeof...(Alternatives); }
      inline constexpr uint16_t      index() const { return _which; }

      template <size_t Index>
      inline constexpr auto&& get_check() {
         // TODO add outcome stuff
         return 3;
      }

      template <size_t Index>
      inline constexpr const auto& get() const & {
         return detail::variant_storage_get<Index>(_storage);
      }

      template <typename Alt>
      inline constexpr const Alt& get() const & {
         return detail::variant_storage_get<detail::get_alternatives_index_v<Alt, Alternatives...>>(_storage);
      }

      template <size_t Index>
      inline constexpr const auto&& get() const && {
         return detail::variant_storage_get<Index>(std::move(_storage));
      }

      template <typename Alt>
      inline constexpr const Alt&& get() const && {
         return detail::variant_storage_get<detail::get_alternatives_index_v<Alt, Alternatives...>>(std::move(_storage));
      }

      template <size_t Index>
      inline constexpr auto&& get() && {
         return detail::variant_storage_get<Index>(std::move(_storage));
      }

      template <typename Alt>
      inline constexpr Alt&& get() && {
         return detail::variant_storage_get<detail::get_alternatives_index_v<Alt, Alternatives...>>(std::move(_storage));
      }

      template <size_t Index>
      inline constexpr auto& get() & {
         return detail::variant_storage_get<Index>(_storage);
      }

      template <typename Alt>
      inline constexpr Alt& get() & {
         return detail::variant_storage_get<detail::get_alternatives_index_v<Alt, Alternatives...>>(_storage);
      }

      template <typename Alt>
      inline constexpr bool is_a() const {
         return _which == detail::get_alternatives_index_v<Alt, Alternatives...>;
      }
      inline constexpr void toggle_exiting_which() { _which ^= 0x100; }
      inline constexpr void clear_exiting_which() { _which &= 0xFF; }
      inline constexpr void set_exiting_which() { _which |= 0x100; }

    private:
      static constexpr size_t _sizeof  = detail::max_layout_size_v<Alternatives...>;
      static constexpr size_t _alignof = detail::max_alignof_v<Alternatives...>;
      uint16_t _which                  = 0;
      detail::variant_storage<Alternatives...> _storage;
   };

} // namespace forge::vm::wasm

export namespace forge::vm::wasm {
   enum opcodes {
      FORGE_VM_WASM_CONTROL_FLOW_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_BR_TABLE_OP(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_RETURN_OP(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_CALL_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_CALL_IMM_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_PARAMETRIC_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_VARIABLE_ACCESS_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_MEMORY_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_I32_CONSTANT_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_I64_CONSTANT_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_F32_CONSTANT_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_F64_CONSTANT_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_COMPARISON_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_NUMERIC_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_CONVERSION_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_EXIT_OP(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_EMPTY_OPS(FORGE_VM_WASM_CREATE_ENUM)
      FORGE_VM_WASM_ERROR_OPS(FORGE_VM_WASM_CREATE_ENUM)
   };

   struct opcode_utils {
      std::map<uint16_t, std::string> opcode_map{
         FORGE_VM_WASM_CONTROL_FLOW_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_BR_TABLE_OP(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_RETURN_OP(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_CALL_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_CALL_IMM_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_PARAMETRIC_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_VARIABLE_ACCESS_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_MEMORY_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_I32_CONSTANT_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_I64_CONSTANT_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_F32_CONSTANT_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_F64_CONSTANT_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_COMPARISON_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_NUMERIC_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_CONVERSION_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_EXIT_OP(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_EMPTY_OPS(FORGE_VM_WASM_CREATE_MAP)
         FORGE_VM_WASM_ERROR_OPS(FORGE_VM_WASM_CREATE_MAP)
      };
   };

   enum imm_types {
      none,
      block_imm,
      varuint32_imm,
      br_table_imm,
   };


   FORGE_VM_WASM_CONTROL_FLOW_OPS(FORGE_VM_WASM_CREATE_CONTROL_FLOW_TYPES)
   FORGE_VM_WASM_BR_TABLE_OP(FORGE_VM_WASM_CREATE_BR_TABLE_TYPE)
   FORGE_VM_WASM_RETURN_OP(FORGE_VM_WASM_CREATE_CONTROL_FLOW_TYPES)
   FORGE_VM_WASM_CALL_OPS(FORGE_VM_WASM_CREATE_CALL_TYPES)
   FORGE_VM_WASM_CALL_IMM_OPS(FORGE_VM_WASM_CREATE_CALL_IMM_TYPES)
   FORGE_VM_WASM_PARAMETRIC_OPS(FORGE_VM_WASM_CREATE_TYPES)
   FORGE_VM_WASM_VARIABLE_ACCESS_OPS(FORGE_VM_WASM_CREATE_VARIABLE_ACCESS_TYPES)
   FORGE_VM_WASM_MEMORY_OPS(FORGE_VM_WASM_CREATE_MEMORY_TYPES)
   FORGE_VM_WASM_I32_CONSTANT_OPS(FORGE_VM_WASM_CREATE_I32_CONSTANT_TYPE)
   FORGE_VM_WASM_I64_CONSTANT_OPS(FORGE_VM_WASM_CREATE_I64_CONSTANT_TYPE)
   FORGE_VM_WASM_F32_CONSTANT_OPS(FORGE_VM_WASM_CREATE_F32_CONSTANT_TYPE)
   FORGE_VM_WASM_F64_CONSTANT_OPS(FORGE_VM_WASM_CREATE_F64_CONSTANT_TYPE)
   FORGE_VM_WASM_COMPARISON_OPS(FORGE_VM_WASM_CREATE_TYPES)
   FORGE_VM_WASM_NUMERIC_OPS(FORGE_VM_WASM_CREATE_TYPES)
   FORGE_VM_WASM_CONVERSION_OPS(FORGE_VM_WASM_CREATE_TYPES)
   FORGE_VM_WASM_EXIT_OP(FORGE_VM_WASM_CREATE_EXIT_TYPE)
   FORGE_VM_WASM_EMPTY_OPS(FORGE_VM_WASM_CREATE_TYPES)
   FORGE_VM_WASM_ERROR_OPS(FORGE_VM_WASM_CREATE_TYPES)

   using opcode = variant<
      FORGE_VM_WASM_CONTROL_FLOW_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_BR_TABLE_OP(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_RETURN_OP(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_CALL_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_CALL_IMM_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_PARAMETRIC_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_VARIABLE_ACCESS_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_MEMORY_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_I32_CONSTANT_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_I64_CONSTANT_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_F32_CONSTANT_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_F64_CONSTANT_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_COMPARISON_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_NUMERIC_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_CONVERSION_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_EXIT_OP(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_EMPTY_OPS(FORGE_VM_WASM_IDENTITY)
      FORGE_VM_WASM_ERROR_OPS(FORGE_VM_WASM_IDENTITY_END)
      >;
} // namespace forge::vm::wasm

export namespace forge::vm::wasm {
   namespace detail {
      template <typename T, typename Allocator>
      class vector {
         public:
            constexpr vector(Allocator& allocator, size_t size=0) :
               _size(size),
               _allocator(&allocator),
               _data(allocator.template alloc<T>( _size )) {
            }

            constexpr vector(const vector& mv) = delete;
            constexpr vector(vector&& mv) = default;
            constexpr vector& operator=(vector&& mv) = default;

            constexpr inline void resize( size_t size ) {
               if (size > _size) {
                  _data = _allocator->template alloc<T>( size );
               } else {
                  _allocator->template reclaim<T>( _data + size, _size - size );
               }
               _size = size;
            }
            template <typename U, typename = std::enable_if_t<std::is_same_v<T, std::decay_t<U>>, int>>
            constexpr inline void push_back( U&& val ) {
               // if the vector is unbounded don't assert
              if ( _index >= _size )
                  resize( _size * 2 );
               _data[_index++] = std::forward<U>(val);
            }
            constexpr inline void emplace_back( T&& val ) {
               // if the vector is unbounded don't assert
              if ( _index >= _size )
                  resize( _size * 2 );
               _data[_index++] = std::move(val);
            }

            constexpr inline void back() {
               return _data[_index];
            }

            constexpr inline void pop_back() {
               detail::check<exceptions::vector_out_of_bounds>((_index >= 0), "vector pop out of bounds");
               _index--;
            }

            constexpr inline T& at( size_t i ) {
               detail::check<exceptions::vector_out_of_bounds>((i < _size), "vector read out of bounds");
               return _data[i];
            }

            constexpr inline T& at( size_t i )const {
               detail::check<exceptions::vector_out_of_bounds>((i < _size), "vector read out of bounds");
               return _data[i];
            }

            constexpr inline T& at_no_check( size_t i ) {
               return _data[i];
            }

            constexpr inline T& at_no_check( size_t i ) const {
               return _data[i];
            }

            constexpr inline T& operator[] (size_t i) const { return at(i); }
            constexpr inline T& operator[] (size_t i) { return at(i); }
            constexpr inline T* raw() const { return _data; }
            constexpr inline T* data() const { return _data; }
            constexpr inline size_t size() const { return _size; }
            constexpr inline void set( T* data, size_t size, size_t index=-1 ) { _size = size; _data = data; _index = index == -1 ? size - 1 : index; }
            constexpr inline void copy( T* data, size_t size ) {
              resize(size);
              std::copy_n(data, size, _data);
              _index = size-1;
            }

         private:
            size_t _size  = 0;
            Allocator* _allocator = nullptr;
            T*     _data  = nullptr;
            size_t _index = 0;
      };

      struct unmanaged_base_member {
         using allocator = contiguous_allocator;
         unmanaged_base_member(size_t sz) : alloc(sz) {}
         allocator alloc;
      };
   } // ns detail

   template <typename T, typename Allocator>
   class managed_vector : public detail::vector<T, Allocator> {
      public:
         using detail::vector<T, Allocator>::vector;
         constexpr inline void set_owner( Allocator& alloc ) { detail::vector<T, Allocator>::_allocator = &alloc; }
   };

   template <typename T>
   using unmanaged_vector = std::vector<T>;

   template <typename T>
   std::string vector_to_string( T&& vec ) {
     std::string str;
     str.reserve(vec.size());
     for (int i=0; i < vec.size(); i++)
       str[i] = vec[i];
     return str;
   }
} // namespace forge::vm::wasm

export namespace forge::vm::wasm {
   // helper to read a wasm file into a vector of bytes
   inline std::vector<uint8_t> read_wasm(const std::string& fname) {
      std::ifstream wasm_file(fname, std::ios::binary);
      if (!wasm_file.is_open())
         throw std::runtime_error("wasm file not found");
      wasm_file.seekg(0, std::ios::end);
      std::vector<uint8_t> wasm;
      int                  len = wasm_file.tellg();
      if (len < 0)
         throw std::runtime_error("wasm file length is -1");
      wasm.resize(len);
      wasm_file.seekg(0, std::ios::beg);
      wasm_file.read((char*)wasm.data(), wasm.size());
      wasm_file.close();
      return wasm;
   }

   // forward declarations
   struct i32_const_t;
   struct i64_const_t;
   struct f32_const_t;
   struct f64_const_t;

   template <typename StackElem>
   inline void print_result(const std::optional<StackElem>& result) {
      if(result) {
         std::cout << "result: ";
         if (result->template is_a<i32_const_t>())
            std::cout << "i32:" << result->to_ui32();
         else if (result->template is_a<i64_const_t>())
            std::cout << "i64:" << result->to_ui64();
         else if (result->template is_a<f32_const_t>())
            std::cout << "f32:" << result->to_f32();
         else if (result->template is_a<f64_const_t>())
           std::cout << "f64:" << result->to_f64();
         std::cout << std::endl;
     }
   }

   // helpers for std::visit
   template <class... Ts>
   struct overloaded : Ts... {
      using Ts::operator()...;
   };
   template <class... Ts>
   overloaded(Ts...)->overloaded<Ts...>;

   // helpers for handling void returns
   struct maybe_void_t {
      template <typename T>
      inline constexpr friend T&& operator, (T&& val, maybe_void_t) {return std::forward<T>(val);}
   };

   inline maybe_void_t maybe_void;

   // simple utility function to demangle C++ type names
   inline std::string demangle(const char* mangled_name) {
      size_t                                          len    = 0;
      int                                             status = 0;
      ::std::unique_ptr<char, decltype(&::std::free)> ptr(
            __cxxabiv1::__cxa_demangle(mangled_name, nullptr, &len, &status), &::std::free);
      return ptr.get();
   }

   template<typename... T>
   void ignore_unused_variable_warning(T&...) {}

   // helpers for creating subtuples
   namespace detail {
      template <size_t N, size_t I, typename T, typename... Ts>
      struct subtuple_impl;

      template <size_t N, size_t I, typename T, typename... Ts>
      struct subtuple_impl <N, I, std::tuple<T, Ts...>> {
         using type = decltype( std::tuple_cat( std::declval<std::tuple<T>>(),
                  std::declval<typename subtuple_impl<N, I+1, std::tuple<Ts...>>::type>() ) );
      };

      template <size_t N, typename T, typename... Ts>
      struct subtuple_impl <N, N, std::tuple<T, Ts...>> {
         using type = std::tuple<T>;
      };

      template <size_t N, typename T>
      using subtuple_t = typename subtuple_impl<N, 0, T>::type;

      template <typename>
      struct generate_subtuples_impl;

      template <typename T, typename... Ts>
      struct generate_subtuples_impl<std::tuple<T, Ts...>> {
         template <size_t... Is>
         static constexpr auto value( std::index_sequence<Is...> ) {
            return std::make_tuple(std::declval<subtuple_t<Is, std::tuple<T, Ts...>>>()...);
         }
      };

      template <typename T>
      using generate_subtuples_t = decltype(generate_subtuples_impl<T>::value( std::make_index_sequence<std::tuple_size_v<T>>{} ));

      template <typename T, typename... Ts>
      struct generate_all_subtuples_impl;

      template <typename T, typename... Ts>
      struct generate_all_subtuples_impl<std::tuple<T, Ts...>> {
         using type = decltype( std::tuple_cat( std::declval<typename generate_all_subtuples_impl<std::tuple<Ts...>>::type>(),
                                                std::declval<generate_subtuples_t<std::tuple<T, Ts...>>>() ) );
      };

      template <>
      struct generate_all_subtuples_impl<std::tuple<>> {
         using type = std::tuple<>;
      };

      template <typename Tuple>
      constexpr auto generate_all_subtuples( Tuple&& tup) {
         return std::declval<typename generate_all_subtuples_impl<Tuple>::type>();
      }

      template <size_t N>
      struct tuple_index {
         static constexpr size_t value = N;
      };

      template <size_t N, size_t I, typename Insert, typename Tuple>
      struct insert_type {
         using type = std::tuple<Insert>;
      };

      template <size_t N, size_t I, typename Insert, template<typename...> class Tuple, typename T, typename... Ts>
      struct insert_type<N, I, Insert, Tuple<T, Ts...>> {
         using type = decltype(std::tuple_cat(std::tuple<T>{}, typename insert_type<N, I+1, Insert, Tuple<Ts...>>::type{}));
      };

      template <size_t N, typename Insert, template<typename...> class Tuple, typename T, typename... Ts>
      struct insert_type<N, N, Insert, Tuple<T, Ts...>> {
         using type = std::tuple<Insert, T, Ts...>;
      };

      template <size_t I, typename Tuple, typename Indices>
      constexpr auto get_tuple_size_from_index() {
         if constexpr (I >= std::tuple_size_v<Tuple>)
            return -1;
         else
            return std::tuple_size_v<std::tuple_element_t<std::tuple_element_t<I, Indices>::value, Tuple>>;
      }

      template <size_t I, typename Tuple>
      constexpr auto get_tuple_size() {
         if constexpr (I >= std::tuple_size_v<Tuple>)
            return -1;
         else
            return std::tuple_size_v<std::tuple_element_t<I, Tuple>>;
      }

      template <size_t N, typename Insert, typename Tuple>
      using insert_type_t = typename insert_type<N, 0, Insert, Tuple>::type;

      template <size_t N, size_t I, typename Tuple, typename Indices>
      struct index_inserter {
         static constexpr int32_t size_of_element = get_tuple_size<N, Tuple>();

         template <size_t M = N, size_t J = I>
         static constexpr auto value(std::enable_if_t<M!=J, int> = 0) {
            if constexpr (size_of_element > get_tuple_size_from_index<I, Tuple, Indices>())
               return insert_type_t<I, tuple_index<N>, Indices>();
            else
               return index_inserter<N, I+1, Tuple, Indices>::value();
         }

         template <size_t M = N, size_t J = I>
         static constexpr auto value(std::enable_if_t<M==J, int> = 0) {
            return std::tuple_cat(Indices{}, std::tuple<tuple_index<N>>());
         }
      };

      template <size_t N, typename Tuple, typename Indices>
      using index_insert_t = decltype(index_inserter<N, 0, Tuple, Indices>::value());

      template <typename Tuple, typename Indices, size_t... Is>
      constexpr auto reorder_tuple(std::index_sequence<Is...>) {
         static_assert(std::tuple_size_v<Tuple> == std::tuple_size_v<Indices>);
         return std::tuple<std::tuple_element_t<std::tuple_element_t<Is, Indices>::value, Tuple>...>();
      }

      template <typename Tuple, typename Indices>
      using reorder_tuple_t = decltype(reorder_tuple<Tuple, Indices>(std::make_index_sequence<std::tuple_size_v<Indices>-1>{}));

      // sort the tuple by largest subtuple to smallest subtuple
      template <size_t N, size_t I, typename Tuple, typename Indices>
      constexpr auto tuple_sort() {
         if constexpr (N == I)
            return reorder_tuple_t<Tuple, index_insert_t<N, Tuple, Indices>>();
         else
            return tuple_sort<N, I+1, Tuple, index_insert_t<I, Tuple, Indices>>();
      }

      template <size_t N, size_t I, typename Tuple>
      struct tuple_trim;

      template <size_t N, size_t I, template <typename...> class Tuple, typename T, typename... Ts>
      struct tuple_trim<N, I, Tuple<T, Ts...>> {
         using type = typename tuple_trim<N, I+1, Tuple<Ts...>>::type;
      };

      template <size_t N, template <typename...> class Tuple, typename T, typename... Ts>
      struct tuple_trim<N, N, Tuple<T, Ts...>> {
         using type = Tuple<T, Ts...>;
      };
   } // namespace detail

   template <typename Tuple>
   using generate_all_subtuples_t = decltype(detail::generate_all_subtuples(std::declval<Tuple>()));

   // sort the tuple of subtuples by largest subtuple to smallest subtuple
   template <typename Tuple>
   using tuple_sort_t = decltype(detail::tuple_sort<std::tuple_size_v<Tuple>-1, 0, Tuple, std::tuple<>>());

   template <size_t N, typename Tuple>
   using tuple_trim_t = typename detail::tuple_trim<N, 0, Tuple>::type;
} // namespace forge::vm::wasm

export namespace forge::vm::wasm {
   template <typename T>
   struct guarded_ptr {
      T* raw_ptr;
      T* orig_ptr;
      T* bnds;
      guarded_ptr( T* rp, size_t bnds ) : raw_ptr(rp), orig_ptr(rp), bnds(rp + bnds) {}

      inline guarded_ptr& operator+=(size_t i) {
         detail::check<exceptions::pointer_out_of_bounds>((i <= static_cast<std::size_t>(bnds - raw_ptr)), "overbounding pointer");
         raw_ptr += i;
         return *this;
      }

      inline guarded_ptr& operator++() {
         detail::check<exceptions::pointer_out_of_bounds>((raw_ptr < bnds), "overbounding pointer");
         raw_ptr += 1;
         return *this;
      }

      inline guarded_ptr operator++(int) {
         guarded_ptr tmp = *this;
         ++*this;
         return tmp;
      }

      friend inline guarded_ptr operator+(const guarded_ptr& arg, size_t i) {
         guarded_ptr tmp = arg;
         tmp += i;
         return tmp;
      }
      friend inline guarded_ptr operator+(std::size_t i, const guarded_ptr& arg) {
         guarded_ptr tmp = arg;
         tmp += i;
         return tmp;
      }

      inline T& operator* () const {
         detail::check<exceptions::pointer_out_of_bounds>((raw_ptr < bnds), "accessing out of bounds");
         return *raw_ptr;
      }

      inline T* operator-> () const {
         detail::check<exceptions::pointer_out_of_bounds>((raw_ptr < bnds), "accessing out of bounds");
         return raw_ptr;
      }

      T& operator= (const guarded_ptr<T>& ptr) = delete;

      inline T* raw() {
         return raw_ptr;
      }

      inline size_t offset() {
         return raw_ptr - orig_ptr;
      }

      // reduces the bounds for the lifetime of the returned object
      auto scoped_shrink_bounds(std::size_t n) {
         detail::check<exceptions::pointer_out_of_bounds>((n <= static_cast<std::size_t>(bnds - raw_ptr)), "guarded ptr out of bounds");
         T* old_bnds = bnds;
         bnds = raw_ptr + n;
         return scope_guard{ [this, old_bnds](){ bnds = old_bnds; } };
      }
      // verifies that the pointer is advanced by exactly n before
      // the returned object is destroyed.
      auto scoped_consume_items(std::size_t n) {
         detail::check<exceptions::pointer_out_of_bounds>((n <= static_cast<std::size_t>(bnds - raw_ptr)), "guarded ptr out of bounds");
         int exceptions = std::uncaught_exceptions();
         T* old_bnds = bnds;
         bnds = raw_ptr + n;
         struct throwing_destructor { ~throwing_destructor() noexcept(false) {} };
         throwing_destructor x;
         return scope_guard{ [this, old_bnds, exceptions, x](){
            detail::check<exceptions::pointer_out_of_bounds>((exceptions != std::uncaught_exceptions() || raw_ptr == bnds), "guarded_ptr not advanced");
            bnds = old_bnds;
         } };
      }

      inline size_t bounds() {
         return bnds - orig_ptr;
      }

      inline T at(size_t index) const {
         detail::check<exceptions::pointer_out_of_bounds>((index < static_cast<std::size_t>(bnds - raw_ptr)), "accessing out of bounds");
         return raw_ptr[index];
      }

      inline T at() const {
         detail::check<exceptions::pointer_out_of_bounds>((raw_ptr < bnds), "accessing out of bounds");
         return *raw_ptr;
      }

      inline T operator[](size_t index) const {
         return at(index);
      }
   };
} // ns forge::vm::wasm

export namespace forge::vm::wasm {

   class operand_stack_elem : public variant<i32_const_t, i64_const_t, f32_const_t, f64_const_t> {
      public:
         using variant<i32_const_t, i64_const_t, f32_const_t, f64_const_t>::variant;
         inline int32_t&  to_i32() & { return get<i32_const_t>().data.i; }
         inline uint32_t& to_ui32() & { return get<i32_const_t>().data.ui; }
         inline float&    to_f32() & { return get<f32_const_t>().data.f; }
         inline uint32_t& to_fui32() & { return get<f32_const_t>().data.ui; }

         inline int64_t&  to_i64() & { return get<i64_const_t>().data.i; }
         inline uint64_t& to_ui64() & { return get<i64_const_t>().data.ui; }
         inline double&   to_f64() & { return get<f64_const_t>().data.f; }
         inline uint64_t& to_fui64() & { return get<f64_const_t>().data.ui; }

         inline int32_t  to_i32() const & { return get<i32_const_t>().data.i; }
         inline uint32_t to_ui32() const & { return get<i32_const_t>().data.ui; }
         inline float    to_f32() const & { return get<f32_const_t>().data.f; }
         inline uint32_t to_fui32() const & { return get<f32_const_t>().data.ui; }

         inline int64_t  to_i64() const & { return get<i64_const_t>().data.i; }
         inline uint64_t to_ui64() const & { return get<i64_const_t>().data.ui; }
         inline double   to_f64() const & { return get<f64_const_t>().data.f; }
         inline uint64_t to_fui64() const & { return get<f64_const_t>().data.ui; }

   };
} // nameo::vm

/*
 * definitions from https://github.com/WebAssembly/design/blob/master/BinaryEncoding.md
 */



export namespace forge::vm::wasm {
   enum types { i32 = 0x7f, i64 = 0x7e, f32 = 0x7d, f64 = 0x7c, anyfunc = 0x70, func = 0x60, pseudo = 0x40, ret_void };

   enum external_kind { Function = 0, Table = 1, Memory = 2, Global = 3 };

   typedef uint8_t value_type;
   typedef uint8_t block_type;
   typedef uint8_t elem_type;

   template <typename T>
   using guarded_vector = managed_vector<T, growable_allocator>;

   struct activation_frame {
      opcode* pc;
      uint32_t last_op_index;
   };

   struct resizable_limits {
      bool     flags;
      uint32_t initial;
      uint32_t maximum = 0;
   };

   struct func_type {
      value_type                 form; // value for the func type constructor
      guarded_vector<value_type> param_types;
      uint8_t                    return_count;
      value_type                 return_type;
   };

   inline bool operator==(const func_type& lhs, const func_type& rhs) {
      return lhs.form == rhs.form &&
        lhs.param_types.size() == rhs.param_types.size() &&
        std::equal(lhs.param_types.raw(), lhs.param_types.raw() + lhs.param_types.size(), rhs.param_types.raw()) &&
        lhs.return_count == rhs.return_count &&
        (lhs.return_count || lhs.return_type == rhs.return_type);
   }

   union expr_value {
      int32_t  i32;
      int64_t  i64;
      uint32_t f32;
      uint64_t f64;
   };

   struct init_expr {
      int8_t     opcode;
      expr_value value;
   };

   struct global_type {
      value_type content_type;
      bool       mutability;
   };

   struct global_variable {
      global_type type;
      init_expr   init;
   };

   struct table_type {
      elem_type                element_type;
      resizable_limits         limits;
      guarded_vector<uint32_t> table;
   };

   struct memory_type {
      resizable_limits limits;
   };

   union import_type {
      import_type() {}
      uint32_t    func_t;
      table_type  table_t;
      memory_type mem_t;
      global_type global_t;
   };

   struct import_entry {
      guarded_vector<uint8_t> module_str;
      guarded_vector<uint8_t> field_str;
      external_kind           kind;
      import_type             type;
   };

   struct export_entry {
      guarded_vector<uint8_t> field_str;
      external_kind           kind;
      uint32_t                index;
   };

   struct elem_segment {
      uint32_t                 index;
      init_expr                offset;
      guarded_vector<uint32_t> elems;
   };

   struct local_entry {
      uint32_t   count;
      value_type type;
   };

   union native_value {
      native_value() = default;
      constexpr native_value(uint32_t arg) : i32(arg) {}
      constexpr native_value(uint64_t arg) : i64(arg) {}
      constexpr native_value(float arg) : f32(arg) {}
      constexpr native_value(double arg) : f64(arg) {}
      uint32_t i32;
      uint64_t i64;
      float f32;
      double f64;
   };

   struct function_body {
      uint32_t                    size;
      guarded_vector<local_entry> locals;
      opcode*                     code;
      std::size_t                 jit_code_offset;
   };

   struct data_segment {
      uint32_t                index;
      init_expr               offset;
      guarded_vector<uint8_t> data;
   };

   using wasm_code     = std::vector<uint8_t>;
   using wasm_code_ptr = guarded_ptr<uint8_t>;
   typedef std::uint32_t  wasm_ptr_t;
   typedef std::uint32_t  wasm_size_t;

   struct name_assoc {
      std::uint32_t idx;
      guarded_vector<uint8_t> name;
   };
   struct indirect_name_assoc {
      std::uint32_t idx;
      guarded_vector<name_assoc> namemap;
   };
   struct name_section {
      guarded_vector<uint8_t>* module_name = nullptr;
      guarded_vector<name_assoc>* function_names = nullptr;
      guarded_vector<indirect_name_assoc>* local_names = nullptr;
   };

   struct module {
      growable_allocator              allocator;
      uint32_t                        start     = std::numeric_limits<uint32_t>::max();
      guarded_vector<func_type>       types     = { allocator, 0 };
      guarded_vector<import_entry>    imports   = { allocator, 0 };
      guarded_vector<uint32_t>        functions = { allocator, 0 };
      guarded_vector<table_type>      tables    = { allocator, 0 };
      guarded_vector<memory_type>     memories  = { allocator, 0 };
      guarded_vector<global_variable> globals   = { allocator, 0 };
      guarded_vector<export_entry>    exports   = { allocator, 0 };
      guarded_vector<elem_segment>    elements  = { allocator, 0 };
      guarded_vector<function_body>   code      = { allocator, 0 };
      guarded_vector<data_segment>    data      = { allocator, 0 };

      // Custom sections:
      name_section* names = nullptr;

      // not part of the spec for WASM
      guarded_vector<uint32_t> import_functions = { allocator, 0 };
      guarded_vector<uint32_t> type_aliases     = { allocator, 0 };
      guarded_vector<uint32_t> fast_functions   = { allocator, 0 };
      uint64_t                 maximum_stack    = 0;
      // If non-null, indicates that the parser encountered an error
      // that would prevent successful instantiation.  Must refer
      // to memory with static storage duration.
      const char *             error            = nullptr;

      // Stores data needed by JIT execution, in memory managed by standard
      // C++ vectors, not growable_allocator used by guarded_vector,
      // such that growable_allocator can be released after parsing.
      // All growable_allocators (including recursively) used in module
      // are redefined by std::vector in jit_mod_t.
      // This is to make it possible parsing WASM only once for JIT.
      struct jit_mod_t {
         struct jit_func_type {
            value_type                 form;
            std::vector<value_type>    param_types;
            uint8_t                    return_count;
            value_type                 return_type;
         };
         struct jit_import_type {
            uint32_t    func_t;
         };
         struct jit_import_entry {
            std::vector<uint8_t> module_str;
            std::vector<uint8_t> field_str;
            external_kind        kind;
            jit_import_type      type;
         };
         struct jit_export_entry {
            std::vector<uint8_t> field_str;
            external_kind        kind;
            uint32_t             index;
         };
         struct jit_data_segment {
            uint32_t              index;
            init_expr             offset;
            std::vector<uint8_t>  data;
         };

         std::vector<jit_func_type>        types;
         std::vector<jit_import_entry>     imports;
         std::vector<uint32_t>             functions;
         // tables not needed during JIT execution
         std::vector<memory_type>          memories;
         std::vector<global_variable>      globals;
         std::vector<jit_export_entry>     exports;
         // elements not needed during JIT execution
         std::vector<size_t>               jit_code_offset;
         std::vector<jit_data_segment>     data;
         std::vector<uint32_t>             import_functions;
         // type_aliases and fast_functions not needed during JIT execution

         auto& get_function_type(uint32_t index) const {
            if (index < get_imported_functions_size())
               return types[imports[index].type.func_t];
            return types[functions[index - get_imported_functions_size()]];
         }
         uint32_t get_imported_functions_size() const {
            return get_imported_functions_size_impl(imports);
         }
      };

      // The memory storing module data for JIT
      std::unique_ptr<jit_mod_t> jit_mod;

      // Constructs data for JIT execution.
      // Called from backend::construct() after parsing is finalized.
      void make_jit_module() {
         jit_mod = std::make_unique<jit_mod_t>();

         if (auto types_size = types.size(); types_size > 0) {
            jit_mod->types.reserve(types_size);
            for (uint32_t i = 0; i < types_size; ++i) {
               const auto& type = types[i];
               jit_mod->types.emplace_back(jit_mod_t::jit_func_type{
                  type.form,
                  {type.param_types.data(), type.param_types.data() + type.param_types.size()},
                  type.return_count,
                  type.return_type
               });
            }
         }

         if (auto imports_size = imports.size(); imports_size > 0) {
            jit_mod->imports.reserve(imports_size);
            for (uint32_t i = 0; i < imports_size; ++i) {
               const auto& entry = imports[i];
               jit_mod->imports.emplace_back(jit_mod_t::jit_import_entry{
                  {entry.module_str.data(), entry.module_str.data() + entry.module_str.size()},
                  {entry.field_str.data(), entry.field_str.data() + entry.field_str.size()},
                  entry.kind,
                  {entry.type.func_t}
               });
            }
         }

         if (memories.size() > 0) {
            jit_mod->memories.emplace_back(memories[0]); // memories has one element only
         }

         if (functions.size() > 0) {
            jit_mod->functions.assign(functions.data(), functions.data() + functions.size());
         }

         if (globals.size() > 0) {
            jit_mod->globals.assign(globals.raw(), globals.raw() + globals.size());
         }

         if (auto exports_size = exports.size(); exports_size > 0) {
            jit_mod->exports.reserve(exports_size);
            for (uint32_t i = 0; i < exports_size; ++i) {
               const auto& entry = exports[i];
               jit_mod->exports.emplace_back(jit_mod_t::jit_export_entry{
                  {entry.field_str.data(), entry.field_str.data() + entry.field_str.size()},
                  entry.kind,
                  entry.index
               });
            }
         }

         if (auto code_size = code.size(); code_size > 0) {
            jit_mod->jit_code_offset.reserve(code_size);
            for (uint32_t i = 0; i < code_size; ++i) {
               jit_mod->jit_code_offset.emplace_back(code[i].jit_code_offset);
            }
         }

         if (auto data_size = data.size(); data_size > 0) {
            jit_mod->data.reserve(data_size);
            for (uint32_t i = 0; i < data_size; ++i) {
               const auto& data_seg = data[i];
               jit_mod->data.emplace_back(jit_mod_t::jit_data_segment{
                  data_seg.index,
                  data_seg.offset,
                  {data_seg.data.data(), data_seg.data.data() + data_seg.data.size()}
               });
            }
         }

         if (import_functions.size() > 0) {
            jit_mod->import_functions.assign(import_functions.data(), import_functions.data() + import_functions.size());
         }
      }

      void finalize() {
         import_functions.resize(get_imported_functions_size());
         allocator.finalize();
      }
      uint32_t get_imported_functions_size() const {
         return get_imported_functions_size_impl(imports);
      }
      template<typename Imports>
      static uint32_t get_imported_functions_size_impl(const Imports& imports) {
         uint32_t number_of_imports = 0;
         const auto sz = imports.size();
         // we don't want to use `imports[i]` or `imports.at(i)` since these do an unnecessary check
         // the redundant loop bounds check. The check is unnecessary since we iterate from `0` to `_size`.
         // So get the pointer to the first element and dereference it directly.
         // ------------------------------------------------------------------------------------------------
         const auto data = imports.data();
         for (uint32_t i = 0; i < sz; i++) {
            if (data[i].kind == external_kind::Function)
               number_of_imports++;
         }
         return number_of_imports;
      }
      inline uint32_t get_functions_size() const { return functions.size(); }
      inline uint32_t get_functions_total() const { return get_imported_functions_size() + get_functions_size(); }
      inline opcode* get_function_pc( uint32_t fidx ) const {
         detail::check<exceptions::interpreter>((fidx >= get_imported_functions_size()), "trying to get the PC of an imported function");
         return code[fidx-get_imported_functions_size()].code;
      }

      inline auto& get_opcode(uint32_t pc) const {
         return ((opcode*)&code[0].code[0])[pc];
      }

      inline uint32_t get_function_locals_size(uint32_t index) const {
         detail::check<exceptions::interpreter>((index >= get_imported_functions_size()), "imported functions do not have locals");
         return code[index - get_imported_functions_size()].locals.size();
      }

      auto& get_function_type(uint32_t index) const {
         if (index < get_imported_functions_size())
            return types[imports[index].type.func_t];
         return types[functions[index - get_imported_functions_size()]];
      }

      // When jit_mod is available, this function executes on jit_mod,
      // otherwise on module itself.
      uint32_t get_exported_function(const std::string_view str) {
         return (jit_mod) ? get_exported_function_impl(jit_mod->exports, str) : get_exported_function_impl(exports, str);
      }

      template<typename Exports>
      static uint32_t get_exported_function_impl(const Exports& exports, const std::string_view str) {
         uint32_t index = std::numeric_limits<uint32_t>::max();
         for (uint32_t i = 0; i < exports.size(); i++) {
            if (exports[i].kind == external_kind::Function && exports[i].field_str.size() == str.size() &&
                memcmp((const char*)str.data(), (const char*)exports[i].field_str.data(), exports[i].field_str.size()) ==
                      0) {
               index = exports[i].index;
               break;
            }
         }
         return index;
      }

      void normalize_types();
   };
} // namespace forge::vm::wasm

/*
 * definitions from https://github.com/WebAssembly/design/blob/master/BinaryEncoding.md
 */


export namespace forge::vm::wasm {
   using std::nullptr_t;

   template <typename ElemT, size_t ElemSz, typename Allocator = nullptr_t >
   class stack {
    public:
      template <typename Alloc=Allocator, typename = std::enable_if_t<std::is_same_v<Alloc, std::nullptr_t>, int>>
      stack()
         : _store(ElemSz) {}

      template <typename Alloc=Allocator, typename = std::enable_if_t<!std::is_same_v<Alloc, nullptr_t>, int>>
      stack(Alloc&& alloc)
         : _store(alloc, ElemSz) {}

      template <typename Alloc=Allocator, typename = std::enable_if_t<!std::is_same_v<Alloc, nullptr_t>, int>>
      stack(uint32_t n, Alloc&& alloc)
         : _store(alloc, n) {}

      void push(ElemT&& e) {
         if constexpr (std::is_same_v<Allocator, nullptr_t>) {
            if (_index >= _store.size())
               _store.resize(_store.size()*2);
         }
         _store[_index++] = std::move(e);
      }

      ElemT pop() { return _store[--_index]; }

      ElemT& get(uint32_t index) const {
         detail::check<exceptions::interpreter>((index <= _index), "invalid stack index");
         return (ElemT&)_store[index];
      }
      void set(uint32_t index, const ElemT& el) {
         detail::check<exceptions::interpreter>((index <= _index), "invalid stack index");
         _store[index] = el;
      }
      void  eat(uint32_t index) { _index = index; }
      // compact the last element to the element pointed to by index
      void compact(uint32_t index) {
         _store[index] = _store[_index-1];
         _index = index+1;
      }
      size_t       current_index() const { return _index; }
      ElemT&       peek() { return _store[_index - 1]; }
      const ElemT& peek() const { return _store[_index - 1]; }
      ElemT&       peek(size_t i) { return _store[_index - 1 - i]; }
      ElemT&       get_back(size_t i) { return _store[_index - 1 - i]; }
      const ElemT& get_back(size_t i)const { return _store[_index - 1 - i]; }
      void         trim(size_t amt) { _index -= amt; }
      size_t       size() const { return _index; }
      size_t       capacity() const { return _store.size(); }

      // This is only applicable when underlying allocator is unmanaged_vector,
      // which is std::vector
      void         reset_capacity() {
         if constexpr (std::is_same_v<Allocator, nullptr_t>) {
            if (_store.capacity() > constants::initial_stack_size) {
               _store.resize(constants::initial_stack_size);
               _store.shrink_to_fit();
            }
         }
      }

    private:
      using base_data_store_t = std::conditional_t<std::is_same_v<Allocator, std::nullptr_t>, unmanaged_vector<ElemT>, managed_vector<ElemT, Allocator>>;

      base_data_store_t _store;
      size_t            _index = 0;
   };

   using operand_stack = stack<operand_stack_elem, constants::initial_stack_size>;
   using call_stack    = stack<activation_frame,   constants::max_call_depth + 1, bounded_allocator>;

} // namespace forge::vm::wasm

export namespace forge::vm::wasm {

   // interface used for the host function system to use
   // clients can create their own interface to overlay their own implementations
   struct execution_interface {
      inline execution_interface( char* memory, operand_stack* os ) : memory(memory), os(os) {}
      inline void* get_memory() const { return memory; }
      inline void trim_operands(std::size_t amt) { os->trim(amt); }

      template <typename T>
      inline void push_operand(T&& op) { os->push(std::forward<T>(op)); }
      inline auto pop_operand() { return os->pop(); }
      inline const auto& operand_from_back(std::size_t index) const { return os->get_back(index); }

      template <typename T>
      inline void* validate_pointer(wasm_ptr_t ptr, wasm_size_t len) const {
         auto result = memory + ptr;
         validate_pointer<T>(result, len);
         return result;
      }

      template <typename T>
      inline void validate_pointer(const void* ptr, wasm_size_t len) const {
         detail::check<exceptions::interpreter>((len <= std::numeric_limits<wasm_size_t>::max() / (wasm_size_t)sizeof(T)), "length will overflow");
         volatile auto check_addr = *(reinterpret_cast<const char*>(ptr) + (len * sizeof(T)) - 1);
         ignore_unused_variable_warning(check_addr);
      }

      inline void* validate_null_terminated_pointer(wasm_ptr_t ptr) const {
         auto result = memory + ptr;
         validate_null_terminated_pointer(result);
         return result;
      }

      inline void validate_null_terminated_pointer(const void* ptr) const {
         volatile auto check_addr = std::strlen(static_cast<const char*>(ptr));
         ignore_unused_variable_warning(check_addr);
      }
      char* memory;
      operand_stack* os;
   };
} // ns forge::vm::wasm
