#pragma once

#include <eosio/ignore.hpp>

#include <string>
#include <tuple>
#include <type_traits>

namespace eosio::detail {

template <typename T> struct unwrap {
   using type = T;
};
template <typename T> struct unwrap<ignore<T>> {
   using type = T;
};

template <typename Result, typename Class, typename... Args>
auto get_args(Result (Class::*)(Args...)) -> std::tuple<std::decay_t<typename unwrap<Args>::type>...>;
template <typename Result, typename Class, typename... Args>
auto get_args(Result (Class::*)(Args...) const) -> std::tuple<std::decay_t<typename unwrap<Args>::type>...>;
template <typename Result, typename Class, typename... Args>
auto get_args_nounwrap(Result (Class::*)(Args...)) -> std::tuple<std::decay_t<Args>...>;
template <typename Result, typename Class, typename... Args>
auto get_args_nounwrap(Result (Class::*)(Args...) const) -> std::tuple<std::decay_t<Args>...>;

template <auto Function> using deduced = decltype(get_args(Function));
template <auto Function> using deduced_nounwrap = decltype(get_args_nounwrap(Function));

template <typename T> struct convert {
   using type = T;
};
template <> struct convert<const char*> {
   using type = std::string;
};
template <> struct convert<char*> {
   using type = std::string;
};

template <typename Expected, typename Actual>
inline constexpr bool compatible_argument =
    (std::is_same_v<Expected, bool> && std::is_integral_v<Actual>) ||
    (std::is_same_v<Actual, bool> && std::is_integral_v<Expected>) ||
    std::is_convertible_v<typename convert<Actual>::type, typename convert<Expected>::type>;

template <auto Function, typename... Args> consteval bool type_check() {
   using expected = deduced<Function>;
   if constexpr (sizeof...(Args) != std::tuple_size_v<expected>) {
      return false;
   } else {
      return []<std::size_t... Index>(std::index_sequence<Index...>) {
         return (compatible_argument<std::tuple_element_t<Index, expected>, Args> && ...);
      }(std::index_sequence_for<Args...>{});
   }
}

template <typename> struct function_traits;
template <typename Class, typename Result, typename... Args>
struct function_traits<Result (Class::*)(Args...)> {
   using return_type = Result;
};
template <typename Class, typename Result, typename... Args>
struct function_traits<Result (Class::*)(Args...) const> {
   using return_type = Result;
};

} // namespace eosio::detail
