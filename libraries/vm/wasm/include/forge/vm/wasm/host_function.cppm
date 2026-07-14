module;

#include <cstddef>
#include <cstdint>
#include <forge/vm/wasm/host_function.hpp>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

export module forge.vm.wasm.host_function;

export import forge.vm.wasm.types;

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

#define FORGE_VM_WASM_HAS_MEMBER(ARG, NAME)                                                                            \
   forge::vm::wasm::detail::overloaded{                                                                                \
       [](auto&& f,                                                                                                    \
          std::enable_if_t<std::is_class_v<std::decay_t<decltype(f)>>&&                                                \
                               forge::vm::wasm::detail::pass_type<decltype(&std::decay_t<decltype(f)>::type::NAME)>(), \
                           int> = 0) constexpr { return true; },                                                       \
       [](...) constexpr { return false; }}(forge::vm::wasm::detail::wrapper_t<decltype(ARG)>{})

#define FORGE_VM_WASM_HAS_MEMBER_TY(TY, NAME)                                                                          \
   forge::vm::wasm::detail::overloaded{                                                                                \
       [](auto&& f,                                                                                                    \
          std::enable_if_t<std::is_class_v<TY>&&                                                                       \
                               forge::vm::wasm::detail::pass_type<decltype(&std::decay_t<decltype(f)>::type::NAME)>(), \
                           int> = 0) constexpr { return true; },                                                       \
       [](...) constexpr { return false; }}(forge::vm::wasm::detail::wrapper_t<TY>{})

#define FORGE_VM_WASM_HAS_TEMPLATE_MEMBER(ARG, NAME)                                                                   \
   forge::vm::wasm::detail::overloaded{                                                                                \
       [&](auto&& f, std::enable_if_t<std::is_class_v<std::decay_t<decltype(f)>>&& forge::vm::wasm::detail::pass_type< \
                                          decltype(&std::decay_t<decltype(f)>::type::template NAME)>(),                \
                                      int> = 0) constexpr { return true; },                                            \
       [](...) constexpr { return false; }}(forge::vm::wasm::detail::wrapper_t<decltype(ARG)>{})

#define FORGE_VM_WASM_HAS_TEMPLATE_MEMBER_TY(TY, NAME)                                                                 \
   forge::vm::wasm::detail::overloaded{                                                                                \
       [](auto&& f, std::enable_if_t<std::is_class_v<TY>&& forge::vm::wasm::detail::pass_type<                         \
                                         decltype(&std::decay_t<decltype(f)>::type::template NAME)>(),                 \
                                     int> = 0) constexpr { return true; },                                             \
       [](...) constexpr { return false; }}(forge::vm::wasm::detail::wrapper_t<TY>{})

// Workaround for compiler bug handling C++g17 auto template parameters.
// The parameter is not treated as being type-dependent in all contexts,
// causing early evaluation of the containing expression.
// Tested at Apple LLVM version 10.0.1 (clang-1001.0.46.4)
#define AUTO_PARAM_WORKAROUND(X) forge::vm::wasm::detail::make_dependent<decltype(X)>(X)

namespace forge::vm::wasm {

struct freestanding {};

namespace detail {
template <typename T> constexpr bool pass_type() {
   return true;
}

template <typename> constexpr bool is_callable_impl(...) {
   return false;
}

template <class... Ts> struct overloaded : Ts... {
   using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

template <typename T> struct wrapper_t {
   using type = T;
   constexpr wrapper_t() {}
   constexpr wrapper_t(T&&) {}
};

template <typename T, typename U> inline constexpr U&& make_dependent(U&& u) {
   return std::forward<U>(u);
}
} // namespace detail

template <typename F> constexpr bool is_callable(F&& fn) {
   return FORGE_VM_WASM_HAS_MEMBER(fn, operator());
}

namespace detail {
template <bool Decay, typename R, typename... Args>
constexpr auto get_types(R(Args...))
    -> std::tuple<R, freestanding, std::tuple<std::conditional_t<Decay, std::decay_t<Args>, Args>...>>;
template <bool Decay, typename R, typename Cls, typename... Args>
constexpr auto get_types(R (Cls::*)(Args...))
    -> std::tuple<R, Cls, std::tuple<std::conditional_t<Decay, std::decay_t<Args>, Args>...>>;
template <bool Decay, typename R, typename Cls, typename... Args>
constexpr auto get_types(R (Cls::*)(Args...) const)
    -> std::tuple<R, Cls, std::tuple<std::conditional_t<Decay, std::decay_t<Args>, Args>...>>;
template <bool Decay, typename R, typename Cls, typename... Args>
constexpr auto get_types(R (Cls::*)(Args...) &)
    -> std::tuple<R, Cls, std::tuple<std::conditional_t<Decay, std::decay_t<Args>, Args>...>>;
template <bool Decay, typename R, typename Cls, typename... Args>
constexpr auto get_types(R (Cls::*)(Args...) &&)
    -> std::tuple<R, Cls, std::tuple<std::conditional_t<Decay, std::decay_t<Args>, Args>...>>;
template <bool Decay, typename R, typename Cls, typename... Args>
constexpr auto get_types(R (Cls::*)(Args...) const&)
    -> std::tuple<R, Cls, std::tuple<std::conditional_t<Decay, std::decay_t<Args>, Args>...>>;
template <bool Decay, typename R, typename Cls, typename... Args>
constexpr auto get_types(R (Cls::*)(Args...) const&&)
    -> std::tuple<R, Cls, std::tuple<std::conditional_t<Decay, std::decay_t<Args>, Args>...>>;
template <bool Decay, typename F> constexpr auto get_types(F&& fn) {
   if constexpr (is_callable(fn))
      return get_types<Decay>(&F::operator());
   else
      return get_types<Decay>(fn);
}

template <bool Decay, typename F> using get_types_t = decltype(get_types<Decay>(std::declval<F>()));

template <std::size_t Sz, std::size_t N, std::size_t I, typename... Args> struct pack_from;

template <std::size_t Sz, std::size_t N, std::size_t I, typename Arg, typename... Args>
struct pack_from<Sz, N, I, Arg, Args...> {
   static_assert(Sz > N, "index out of range");
   using type = typename pack_from<Sz, N, I + 1, Args...>::type;
};

template <std::size_t Sz, std::size_t N, typename Arg, typename... Args> struct pack_from<Sz, N, N, Arg, Args...> {
   using type = std::tuple<Arg, Args...>;
};

template <std::size_t N, typename... Args> using pack_from_t = typename pack_from<sizeof...(Args), N, 0, Args...>::type;

template <std::size_t N, typename R, typename... Args>
constexpr auto parameters_from_impl(R(Args...)) -> pack_from_t<N, Args...>;
template <std::size_t N, typename R, typename Cls, typename... Args>
constexpr auto parameters_from_impl(R (Cls::*)(Args...)) -> pack_from_t<N, Args...>;
template <std::size_t N, typename R, typename Cls, typename... Args>
constexpr auto parameters_from_impl(R (Cls::*)(Args...) const) -> pack_from_t<N, Args...>;
template <std::size_t N, typename R, typename Cls, typename... Args>
constexpr auto parameters_from_impl(R (Cls::*)(Args...) &) -> pack_from_t<N, Args...>;
template <std::size_t N, typename R, typename Cls, typename... Args>
constexpr auto parameters_from_impl(R (Cls::*)(Args...) &&) -> pack_from_t<N, Args...>;
template <std::size_t N, typename R, typename Cls, typename... Args>
constexpr auto parameters_from_impl(R (Cls::*)(Args...) const&) -> pack_from_t<N, Args...>;
template <std::size_t N, typename R, typename Cls, typename... Args>
constexpr auto parameters_from_impl(R (Cls::*)(Args...) const&&) -> pack_from_t<N, Args...>;
template <std::size_t N, typename F> constexpr auto parameters_from_impl(F&& fn) {
   if constexpr (is_callable(fn))
      return parameters_from_impl<N>(&F::operator());
   else
      return parameters_from_impl<N>(fn);
}

template <std::size_t N, typename F>
using parameters_from_impl_t = decltype(parameters_from_impl<N>(std::declval<F>()));
} // namespace detail

template <typename R, typename... Args> constexpr bool is_function(R (*)(Args...)) {
   return true;
}

template <typename F> constexpr bool is_function(F&&) {
   return false;
}

template <typename R, typename Cls, typename... Args> constexpr bool is_member_function(R (Cls::*)(Args...)) {
   return true;
}

template <typename R, typename Cls, typename... Args> constexpr bool is_member_function(R (Cls::*)(Args...) const) {
   return true;
}

template <typename R, typename Cls, typename... Args> constexpr bool is_member_function(R (Cls::*)(Args...) &) {
   return true;
}

template <typename R, typename Cls, typename... Args> constexpr bool is_member_function(R (Cls::*)(Args...) &&) {
   return true;
}

template <typename R, typename Cls, typename... Args> constexpr bool is_member_function(R (Cls::*)(Args...) const&) {
   return true;
}

template <typename R, typename Cls, typename... Args> constexpr bool is_member_function(R (Cls::*)(Args...) const&&) {
   return true;
}

template <typename F> constexpr bool is_member_function(F&&) {
   return false;
}

template <auto FN> inline constexpr static bool is_function_v = is_function(AUTO_PARAM_WORKAROUND(FN));

template <auto FN> inline constexpr static bool is_member_function_v = is_member_function(AUTO_PARAM_WORKAROUND(FN));

template <typename F> constexpr bool is_class(F&&) {
   return std::is_class_v<F>;
}

template <typename F> constexpr auto return_type(F&& fn) -> std::tuple_element_t<0, detail::get_types_t<false, F>>;

template <auto FN> using return_type_t = decltype(return_type(AUTO_PARAM_WORKAROUND(FN)));

template <typename F>
constexpr auto class_from_member(F&& fn) -> std::tuple_element_t<1, detail::get_types_t<false, F>>;

template <auto FN> using class_from_member_t = decltype(class_from_member(AUTO_PARAM_WORKAROUND(FN)));

template <typename F>
constexpr auto flatten_parameters(F&& fn) -> std::tuple_element_t<2, detail::get_types_t<false, F>>;

template <auto FN> using flatten_parameters_t = decltype(flatten_parameters(AUTO_PARAM_WORKAROUND(FN)));

template <typename F>
constexpr auto decayed_flatten_parameters(F&& fn) -> std::tuple_element_t<2, detail::get_types_t<true, F>>;

template <auto FN> using decayed_flatten_parameters_t = decltype(decayed_flatten_parameters(AUTO_PARAM_WORKAROUND(FN)));

template <std::size_t N, typename F>
constexpr auto parameter_at(F&& fn) -> std::tuple_element_t<N, decltype(flatten_parameters(std::declval<F>()))>;

template <std::size_t N, auto FN> using parameter_at_t = decltype(parameter_at<N>(AUTO_PARAM_WORKAROUND(FN)));

template <std::size_t N, typename F> constexpr auto parameters_from(F&& fn) -> detail::parameters_from_impl_t<N, F>;

template <std::size_t N, auto FN> using parameters_from_t = decltype(parameters_from<N>(AUTO_PARAM_WORKAROUND(FN)));

template <typename F> inline constexpr static std::size_t arity(F&& fn) {
   return std::tuple_size_v<decltype(flatten_parameters(fn))>;
}

template <auto FN> inline constexpr static std::size_t arity_v = arity(FN);
} // namespace forge::vm::wasm

export namespace forge::vm::wasm {

// Used for pointer arguments to intrinsics.
// T should be a type that points to external memory, such as a pointer or span.
// The type that T points to must be trivially copyable.
// argument_proxy copies unaligned memory into zero or more objects of the pointee type.
// If LegacyAlign is non-zero it will only copy the memory if it is not aligned to LegacyAlign.
// If the pointee is mutable, the memory will be written back by the destructor.
template <typename T, std::size_t LegacyAlign = 0> struct argument_proxy;

// specialization of argument_proxy for pointers.
// copies a single element.
template <typename T, std::size_t LegacyAlign> struct argument_proxy<T*, LegacyAlign> {
   static_assert(LegacyAlign % alignof(T) == 0, "Specified alignment must be at least alignment of T");
   static_assert(std::is_trivially_copyable_v<T>, "argument_proxy requires a trivially copyable type");
   inline constexpr argument_proxy(void* ptr) : original_ptr(ptr) {
      if (!LegacyAlign || reinterpret_cast<std::uintptr_t>(original_ptr) % LegacyAlign != 0) {
         copy.emplace();
         memcpy(std::addressof(*copy), original_ptr, sizeof(T));
      }
   }
   inline constexpr argument_proxy(const argument_proxy&) = delete;
   inline constexpr argument_proxy(argument_proxy&& other) : original_ptr(other.original_ptr), copy(other.copy) {
      other.copy.reset();
      other.original_ptr = nullptr;
   }
   inline ~argument_proxy() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
      if constexpr (!std::is_const_v<T>)
         if (copy)
            memcpy(original_ptr, std::addressof(*copy), sizeof(T));
#pragma GCC diagnostic pop
   }
   constexpr operator T*() {
      return get();
   }
   constexpr operator const T*() const {
      return get();
   }

   constexpr T* get() {
      return const_cast<T*>(const_cast<const argument_proxy*>(this)->get());
   }
   constexpr const T* get() const {
      if (copy)
         return std::addressof(*copy);
      else
         return static_cast<const T*>(original_ptr);
   }
   constexpr T* operator->() {
      return get();
   }
   constexpr const T* operator->() const {
      return get();
   }

   static constexpr bool is_legacy() {
      return LegacyAlign != 0;
   }
   using pointee_type = T;
   using proxy_type = T*;
   constexpr const void* get_original_pointer() const {
      return original_ptr;
   }

 private:
   void* original_ptr;
   std::optional<std::remove_cv_t<T>> copy;
};

template <typename T, std::size_t LegacyAlign> struct argument_proxy<span<T>, LegacyAlign> : span<T> {
   static_assert(LegacyAlign % alignof(T) == 0, "Specified alignment must be at least alignment of T");
   static_assert(std::is_trivially_copyable_v<T>, "argument_proxy requires a trivially copyable type");
   using pointee_type = T;
   using proxy_type = span<T>;
   inline constexpr argument_proxy(void* ptr, uint32_t size)
       : original_ptr(ptr), copy(is_aligned(ptr) ? nullptr : new std::remove_cv_t<T>[size]) {
      *static_cast<span<T>*>(this) = span<T>(copy ? copy.get() : static_cast<T*>(ptr), size);
      if (copy)
         memcpy(copy.get(), original_ptr, this->size_bytes());
   }
   inline constexpr argument_proxy(const argument_proxy&) = delete;
   inline constexpr argument_proxy(argument_proxy&&) = default;
   inline ~argument_proxy() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
      if constexpr (!std::is_const_v<T>)
         if (copy)
            memcpy(original_ptr, copy.get(), this->size_bytes());
#pragma GCC diagnostic pop
   }
   static constexpr bool is_legacy() {
      return LegacyAlign != 0;
   }
   constexpr const void* get_original_pointer() const {
      return original_ptr;
   }

 private:
   inline static constexpr bool is_aligned(void* ptr) {
      return reinterpret_cast<std::uintptr_t>(ptr) % LegacyAlign == 0;
   }
   void* original_ptr;
   std::unique_ptr<std::remove_cv_t<T>[]> copy = nullptr;
};

namespace detail {
template <typename T> constexpr inline std::true_type is_argument_proxy_type(argument_proxy<T>) {
   return {};
}
template <typename T, std::size_t LA> constexpr inline std::true_type is_argument_proxy_type(argument_proxy<T, LA>) {
   return {};
}
template <typename T> constexpr inline std::false_type is_argument_proxy_type(T) {
   return {};
}
} // namespace detail

template <typename T>
inline constexpr bool is_argument_proxy_type_v =
    std::is_same_v<decltype(detail::is_argument_proxy_type(std::declval<T>())), std::true_type>;

} // namespace forge::vm::wasm

export namespace forge::vm::wasm {
// types for host functions to use
typedef std::nullptr_t standalone_function_t;
struct no_match_t {};
struct invoke_on_all_t {};

template <typename Host_Type = standalone_function_t, typename Execution_Interface = execution_interface>
struct running_context {
   using running_context_t = running_context<Execution_Interface>;
   inline explicit running_context(Host_Type* host, const Execution_Interface& ei) : host(host), interface(ei) {}
   inline explicit running_context(Host_Type* host, Execution_Interface&& ei) : host(host), interface(ei) {}

   inline void* access(wasm_ptr_t addr = 0) const {
      return (char*)interface.get_memory() + addr;
   }

   inline Execution_Interface& get_interface() {
      return interface;
   }
   inline const Execution_Interface& get_interface() const {
      return interface;
   }

   inline decltype(auto) get_host() {
      return *host;
   }

   template <typename T, typename U> inline auto validate_pointer(U ptr, wasm_size_t len) const {
      return get_interface().template validate_pointer<T>(ptr, len);
   }

   template <typename T> inline auto validate_null_terminated_pointer(T ptr) const {
      return get_interface().validate_null_terminated_pointer(ptr);
   }

   Host_Type* host;
   Execution_Interface interface;
};

// Used to prevent base class overloads of from_wasm from being hidden.
template <typename T> struct tag {};

template <typename Host, typename Execution_Interface = execution_interface>
struct type_converter : public running_context<Host, Execution_Interface> {
   using base_type = running_context<Host, Execution_Interface>;
   using base_type::get_host;
   using base_type::running_context;

   // TODO clean this up and figure out a more elegant way to get this for the macro
   using elem_type = operand_stack_elem;

   FORGE_VM_WASM_FROM_WASM(bool, (uint32_t value)) {
      return value ? 1 : 0;
   }
   uint32_t to_wasm(bool&& value) {
      return value ? 1 : 0;
   }
   template <typename T> no_match_t to_wasm(T&&);

   template <typename T>
   auto from_wasm(wasm_ptr_t ptr, wasm_size_t len, tag<T> = {}) const -> std::enable_if_t<is_span_type_v<T>, T> {
      auto p = this->template validate_pointer<typename T::value_type>(ptr, len);
      return {static_cast<typename T::pointer>(p), len};
   }

   template <typename T>
   auto from_wasm(wasm_ptr_t ptr, wasm_size_t len, tag<T> = {}) const
       -> std::enable_if_t<is_argument_proxy_type_v<T> && is_span_type_v<typename T::proxy_type>, T> {
      auto p = this->template validate_pointer<typename T::pointee_type>(ptr, len);
      return {p, len};
   }

   template <typename T>
   auto from_wasm(wasm_ptr_t ptr, tag<T> = {}) const
       -> std::enable_if_t<is_argument_proxy_type_v<T> && std::is_pointer_v<typename T::proxy_type>, T> {
      auto p = this->template validate_pointer<typename T::pointee_type>(ptr, 1);
      return {p};
   }

   template <typename T> inline decltype(auto) as_value(const elem_type& val) const {
      if constexpr (std::is_integral_v<T> && sizeof(T) == 4)
         return static_cast<T>(val.template get<i32_const_t>().data.ui);
      else if constexpr (std::is_integral_v<T> && sizeof(T) == 8)
         return static_cast<T>(val.template get<i64_const_t>().data.ui);
      else if constexpr (std::is_floating_point_v<T> && sizeof(T) == 4)
         return static_cast<T>(val.template get<f32_const_t>().data.f);
      else if constexpr (std::is_floating_point_v<T> && sizeof(T) == 8)
         return static_cast<T>(val.template get<f64_const_t>().data.f);
      else if constexpr (std::is_void_v<std::decay_t<std::remove_pointer_t<T>>>)
         return base_type::access(val.template get<i32_const_t>().data.ui);
      else
         return no_match_t{};
   }

   template <typename T> inline constexpr auto as_result(T&& val) const {
      using value_type = std::remove_cvref_t<T>;
      if constexpr (std::is_integral_v<value_type> && sizeof(value_type) == 4)
         return i32_const_t{static_cast<uint32_t>(val)};
      else if constexpr (std::is_integral_v<value_type> && sizeof(value_type) == 8)
         return i64_const_t{static_cast<uint64_t>(val)};
      else if constexpr (std::is_floating_point_v<value_type> && sizeof(value_type) == 4)
         return f32_const_t{static_cast<float>(val)};
      else if constexpr (std::is_floating_point_v<value_type> && sizeof(value_type) == 8)
         return f64_const_t{static_cast<double>(val)};
      else if constexpr (std::is_void_v<std::remove_cv_t<std::remove_pointer_t<value_type>>>)
         return i32_const_t{
             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(val) - reinterpret_cast<uintptr_t>(this->access()))};
      else
         return no_match_t{};
   }
};

namespace detail {
template <typename T> constexpr bool is_tag_v = false;
template <typename T> constexpr bool is_tag_v<tag<T>> = true;

template <typename Tuple, std::size_t... N>
std::tuple<std::tuple_element_t<N, Tuple>...> tuple_select(std::index_sequence<N...>);

template <typename T>
using strip_tag =
    std::conditional_t<is_tag_v<std::tuple_element_t<std::tuple_size_v<T> - 1, T>>,
                       decltype(tuple_select<T>(std::make_index_sequence<std::tuple_size_v<T> - 1>())), T>;

template <class TC, typename T>
using from_wasm_type_deducer_t = strip_tag<flatten_parameters_t<&TC::template from_wasm<T>>>;
template <class TC, typename T> using to_wasm_type_deducer_t = decltype(std::declval<TC>().to_wasm(std::declval<T>()));

template <std::size_t N, typename Type_Converter> inline constexpr const auto& pop_value(Type_Converter& tc) {
   return tc.get_interface().operand_from_back(N);
}

template <typename S, typename Type_Converter> constexpr bool has_as_value() {
   return !std::is_same_v<no_match_t, std::decay_t<decltype(std::declval<Type_Converter>().template as_value<S>(
                                          pop_value<0>(std::declval<Type_Converter&>())))>>;
}

template <typename S, typename Type_Converter> constexpr bool has_as_result() {
   return !std::is_same_v<
       no_match_t, std::decay_t<decltype(std::declval<Type_Converter>().template as_result<S>(std::declval<S&&>()))>>;
}

template <typename T, class Type_Converter> inline constexpr std::size_t value_operand_size() {
   if constexpr (has_as_value<T, Type_Converter>())
      return 1;
   else
      return std::tuple_size_v<from_wasm_type_deducer_t<Type_Converter, T>>;
}

template <typename T, class Type_Converter>
inline constexpr std::size_t value_operand_size_v = value_operand_size<T, Type_Converter>();

template <typename Args, std::size_t I, class Type_Converter> inline constexpr std::size_t total_operands() {
   if constexpr (I >= std::tuple_size_v<Args>)
      return 0;
   else {
      constexpr std::size_t sz = value_operand_size_v<std::tuple_element_t<I, Args>, Type_Converter>;
      return sz + total_operands<Args, I + 1, Type_Converter>();
   }
}

template <typename Args, class Type_Converter>
inline constexpr std::size_t total_operands_v = total_operands<Args, 0, Type_Converter>();

template <typename S, typename Type_Converter>
inline constexpr bool has_from_wasm_v = FORGE_VM_WASM_HAS_TEMPLATE_MEMBER_TY(Type_Converter, from_wasm<S>);

template <typename S, typename Type_Converter>
inline constexpr bool has_to_wasm_v = !std::is_same_v<no_match_t, to_wasm_type_deducer_t<Type_Converter, S>>;

template <typename Args, typename S, std::size_t At, class Type_Converter, std::size_t... Is>
inline constexpr decltype(auto) create_value(Type_Converter& tc, std::index_sequence<Is...>) {
   constexpr std::size_t offset = total_operands_v<Args, Type_Converter> - 1;
   if constexpr (has_from_wasm_v<S, Type_Converter>) {
      using arg_types = from_wasm_type_deducer_t<Type_Converter, S>;
      return tc.template from_wasm<S>(
          tc.template as_value<std::tuple_element_t<Is, arg_types>>(pop_value<offset - (At + Is)>(tc))...);
   } else {
      static_assert(has_as_value<S, Type_Converter>(),
                    "no type conversion found for type, define a from_wasm for this type");
      return tc.template as_value<S>(pop_value<offset - At>(tc));
   }
}

template <typename S, typename Type_Converter> inline constexpr std::size_t skip_amount() {
   if constexpr (has_as_value<S, Type_Converter>()) {
      return 1;
   } else {
      return std::tuple_size_v<from_wasm_type_deducer_t<Type_Converter, S>>;
   }
}

template <typename Args, std::size_t At, std::size_t Skip_Amt, class Type_Converter>
inline constexpr auto get_values(Type_Converter& tc) {
   if constexpr (At >= std::tuple_size_v<Args>)
      return std::tuple<>{};
   else {
      using source_t = std::tuple_element_t<At, Args>;
      constexpr std::size_t skip_amt = skip_amount<source_t, Type_Converter>();
      using converted_t = decltype(create_value<Args, source_t, Skip_Amt>(tc, std::make_index_sequence<skip_amt>{}));
      auto tail = get_values<Args, At + 1, Skip_Amt + skip_amt>(tc);
      return std::tuple_cat(
          std::tuple<converted_t>(create_value<Args, source_t, Skip_Amt>(tc, std::make_index_sequence<skip_amt>{})),
          std::move(tail));
   }
}

template <typename Type_Converter, typename T> constexpr auto resolve_result(Type_Converter& tc, T&& val) {
   using direct_result = decltype(tc.as_result(std::forward<T>(val)));
   if constexpr (!std::is_same_v<std::decay_t<direct_result>, no_match_t>) {
      return tc.as_result(std::forward<T>(val));
   } else if constexpr (std::is_pointer_v<std::remove_cvref_t<T>> &&
                        has_to_wasm_v<std::remove_cvref_t<T>, Type_Converter>) {
      return tc.as_result(tc.to_wasm(std::remove_cvref_t<T>{val}));
   } else if constexpr (has_to_wasm_v<T, Type_Converter>) {
      return tc.as_result(tc.to_wasm(std::forward<T>(val)));
   } else {
      return tc.as_result(std::forward<T>(val));
   }
}

template <bool Once, std::size_t Cnt, typename T, typename F> inline constexpr void invoke_on_impl(F&& fn) {
   if constexpr (Once && Cnt == 0) {
      std::invoke(fn);
   }
}

template <bool Once, std::size_t Cnt, typename T, typename F, typename Arg, typename... Args>
inline constexpr void invoke_on_impl(F&& fn, const Arg& arg, const Args&... args) {
   if constexpr (Once) {
      if constexpr (Cnt == 0)
         std::invoke(fn, arg, args...);
   } else {
      if constexpr (std::is_same_v<T, Arg> || std::is_same_v<T, invoke_on_all_t>)
         std::invoke(fn, arg, args...);
      invoke_on_impl<Once, Cnt + 1, T>(std::forward<F>(fn), args...);
   }
}

template <typename Precondition, typename Type_Converter, typename Args, std::size_t... Is>
inline void precondition_runner(Type_Converter& ctx, const Args& args, std::index_sequence<Is...>) {
   Precondition::condition(ctx, std::get<Is>(args)...);
}

template <std::size_t I, typename Preconditions, typename Type_Converter, typename Args>
inline void preconditions_runner(Type_Converter& ctx, const Args& args) {
   constexpr std::size_t args_size = std::tuple_size_v<Args>;
   constexpr std::size_t preconds_size = std::tuple_size_v<Preconditions>;
   if constexpr (I < preconds_size) {
      precondition_runner<std::tuple_element_t<I, Preconditions>>(ctx, args, std::make_index_sequence<args_size>{});
      preconditions_runner<I + 1, Preconditions>(ctx, args);
   }
}
} // namespace detail

template <bool Once, typename T, typename F, typename... Args> void invoke_on(F&& func, const Args&... args) {
   detail::invoke_on_impl<Once, 0, T>(std::forward<F>(func), args...);
}

template <auto F, typename Preconditions, typename Type_Converter, typename Host, typename... Args>
decltype(auto) invoke_impl(Type_Converter& tc, Host* host, Args&&... args) {
   if constexpr (std::is_same_v<Host, standalone_function_t>)
      return std::invoke(F, std::forward<Args>(args)...);
   else
      return std::invoke(F, host, std::forward<Args>(args)...);
}

template <auto F, typename Preconditions, typename Host, typename Args, typename Type_Converter, std::size_t... Is>
decltype(auto) invoke_with_host_impl(Type_Converter& tc, Host* host, Args&& args, std::index_sequence<Is...>) {
   detail::preconditions_runner<0, Preconditions>(tc, args);
   return invoke_impl<F, Preconditions>(tc, host, std::get<Is>(std::forward<Args>(args))...);
}

template <auto F, typename Preconditions, typename Args, typename Type_Converter, typename Host, std::size_t... Is>
decltype(auto) invoke_with_host(Type_Converter& tc, Host* host, std::index_sequence<Is...>) {
   constexpr std::size_t args_size = std::tuple_size_v<decltype(detail::get_values<Args, 0, 0>(tc))>;
   return invoke_with_host_impl<F, Preconditions>(tc, host, detail::get_values<Args, 0, 0>(tc),
                                                  std::make_index_sequence<args_size>{});
}

template <typename Type_Converter, typename T>
void maybe_push_result(Type_Converter& tc, T&& res, std::size_t trim_amt) {
   if constexpr (!std::is_same_v<std::decay_t<T>, maybe_void_t>) {
      tc.get_interface().trim_operands(trim_amt);
      tc.get_interface().push_operand(detail::resolve_result(tc, std::forward<T>(res)));
   } else {
      tc.get_interface().trim_operands(trim_amt);
   }
}

template <typename Cls, auto F, typename Preconditions, typename R, typename Args, typename Type_Converter,
          size_t... Is>
auto create_function(std::index_sequence<Is...>) {
   return std::function<void(Cls*, Type_Converter&)>{[](Cls* self, Type_Converter& tc) {
      maybe_push_result(tc,
                        (invoke_with_host<F, Preconditions, Args>(tc, self, std::index_sequence<Is...>{}), maybe_void),
                        detail::total_operands_v<Args, Type_Converter>);
   }};
}

template <typename T> auto to_wasm_type();
template <> constexpr auto to_wasm_type<i32_const_t>() {
   return types::i32;
}
template <> constexpr auto to_wasm_type<i64_const_t>() {
   return types::i64;
}
template <> constexpr auto to_wasm_type<f32_const_t>() {
   return types::f32;
}
template <> constexpr auto to_wasm_type<f64_const_t>() {
   return types::f64;
}

template <typename TC, typename T>
constexpr auto to_wasm_type_v =
    to_wasm_type<decltype(detail::resolve_result(std::declval<TC&>(), std::declval<T>()))>();
template <typename TC> constexpr auto to_wasm_type_v<TC, void> = types::ret_void;

struct host_function {
   std::vector<value_type> params;
   std::vector<value_type> ret;
};

template <typename Func_type> inline bool operator==(const host_function& lhs, const Func_type& rhs) {
   return lhs.params.size() == rhs.param_types.size() &&
          std::equal(lhs.params.begin(), lhs.params.end(), rhs.param_types.data()) &&
          lhs.ret.size() == rhs.return_count && (lhs.ret.size() == 0 || lhs.ret[0] == rhs.return_type);
}

template <typename TC, typename Args, std::size_t... Is> void get_args(value_type*& out, std::index_sequence<Is...>) {
   ((*out++ = to_wasm_type_v<TC, std::tuple_element_t<Is, Args>>), ...);
}

template <typename Type_Converter, typename T> void get_args(value_type*& out) {
   if constexpr (detail::has_from_wasm_v<T, Type_Converter>) {
      using args_tuple = detail::from_wasm_type_deducer_t<Type_Converter, T>;
      get_args<Type_Converter, args_tuple>(out, std::make_index_sequence<std::tuple_size_v<args_tuple>>());
   } else {
      *out++ = to_wasm_type_v<Type_Converter, T>;
   }
}

template <typename Type_Converter, typename Ret, typename Args, std::size_t... Is>
host_function function_types_provider(std::index_sequence<Is...>) {
   host_function hf;
   hf.params.resize(detail::total_operands_v<Args, Type_Converter>);
#if (defined(__GNUC__) && !defined(__clang__))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
   value_type* iter = hf.params.data();
   (get_args<Type_Converter, std::tuple_element_t<Is, Args>>(iter), ...);
#pragma GCC diagnostic pop
#else
   value_type* iter = hf.params.data();
   (get_args<Type_Converter, std::tuple_element_t<Is, Args>>(iter), ...);
#endif
   if constexpr (to_wasm_type_v<Type_Converter, Ret> != types::ret_void) {
      hf.ret = {to_wasm_type_v<Type_Converter, Ret>};
   }
   return hf;
}

using host_func_pair = std::pair<std::string, std::string>;

struct host_func_pair_hash {
   template <class T, class U> std::size_t operator()(const std::pair<T, U>& p) const {
      return std::hash<T>()(p.first) ^ std::hash<U>()(p.second);
   }
};

template <typename Cls, typename Execution_Interface = execution_interface,
          typename Type_Converter = type_converter<Cls, Execution_Interface>>
struct registered_host_functions {
   using host_type_t = Cls;
   using execution_interface_t = Execution_Interface;
   using type_converter_t = Type_Converter;

   struct mappings {
      std::unordered_map<host_func_pair, uint32_t, host_func_pair_hash> named_mapping;
      std::vector<std::function<void(Cls*, Type_Converter&)>> functions;
      std::vector<host_function> host_functions;
      size_t current_index = 0;

      template <auto F, typename R, typename Args, typename Preconditions>
      void add_mapping(const std::string& mod, const std::string& name) {
         named_mapping[{mod, name}] = current_index++;
         functions.push_back(create_function<Cls, F, Preconditions, R, Args, Type_Converter>(
             std::make_index_sequence<std::tuple_size_v<Args>>()));
         host_functions.push_back(
             function_types_provider<Type_Converter, R, Args>(std::make_index_sequence<std::tuple_size_v<Args>>()));
      }

      static mappings& get() {
         static mappings instance;
         return instance;
      }
   };

   template <auto Func, typename... Preconditions> static void add(const std::string& mod, const std::string& name) {
      using args = flatten_parameters_t<AUTO_PARAM_WORKAROUND(Func)>;
      using res = return_type_t<AUTO_PARAM_WORKAROUND(Func)>;
      using preconditions = std::tuple<Preconditions...>;
      mappings::get().template add_mapping<Func, res, args, preconditions>(mod, name);
   }

   static void resolve(module& mod) {
      if (mod.jit_mod != nullptr) {
         resolve_impl(*mod.jit_mod);
      } else {
         resolve_impl(mod);
      }
   }

   template <typename Module> static void resolve_impl(Module& mod) {
      auto& imports = mod.import_functions;
      auto& current_mappings = mappings::get();
      for (std::size_t i = 0; i < mod.imports.size(); i++) {
         std::string mod_name = std::string((char*)mod.imports[i].module_str.data(), mod.imports[i].module_str.size());
         std::string fn_name = std::string((char*)mod.imports[i].field_str.data(), mod.imports[i].field_str.size());
         detail::check<exceptions::link>((current_mappings.named_mapping.count({mod_name, fn_name})),
                                         std::string("no mapping for imported function ") + fn_name);
         imports[i] = current_mappings.named_mapping[{mod_name, fn_name}];
         detail::check<exceptions::link>((mod.imports[i].kind == Function),
                                         std::string("importing non-function ") + fn_name);
         detail::check<exceptions::link>(
             (current_mappings.host_functions[imports[i]] == mod.types[mod.imports[i].type.func_t]),
             std::string("wrong type for imported function ") + fn_name);
      }
   }

   void operator()(Cls* host, Execution_Interface ei, uint32_t index) {
      const auto& _func = mappings::get().functions[index];
      auto tc = Type_Converter{host, std::move(ei)};
      std::invoke(_func, host, tc);
   }
};
} // namespace forge::vm::wasm
