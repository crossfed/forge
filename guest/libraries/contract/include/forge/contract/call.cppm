module;

#include <forge/contract/internal/intrinsics.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.contract.call;

export import forge.chain.protocol.call_access_mode;
export import forge.chain.protocol.call_data_header;
export import forge.contract.hash_id;

import forge.chain.protocol.values;
import forge.contract.datastream;
import forge.contract.intrinsics;

export namespace forge::contract {

[[nodiscard]] std::int64_t call(chain::protocol::name receiver, std::uint64_t flags, const char* data,
                                std::size_t size);
[[nodiscard]] std::uint32_t get_call_return_value(void* data, std::uint32_t size);
[[nodiscard]] std::uint32_t get_call_data(void* data, std::uint32_t size);
void set_call_return_value(void* data, std::uint32_t size);

using access_mode = chain::protocol::call_access_mode;
enum class support_mode : std::uint8_t { abort_op = 0, no_op = 1 };
struct void_call {};
using chain::protocol::call_data_header;

namespace detail {

template <typename> struct member_function;
template <typename Class, typename Result, typename... Args> struct member_function<Result (Class::*)(Args...)> {
   using result = Result;
   using arguments = std::tuple<std::decay_t<Args>...>;
};
template <typename Class, typename Result, typename... Args>
struct member_function<Result (Class::*)(Args...) const> : member_function<Result (Class::*)(Args...)> {};

template <typename Expected, typename... Args>
concept call_arguments = std::constructible_from<Expected, Args...>;

} // namespace detail

template <hash_id::raw Function, auto Method, access_mode Access = access_mode::read_write,
          support_mode Support = support_mode::abort_op>
class call_wrapper {
 public:
   template <typename Receiver> constexpr explicit call_wrapper(Receiver&& receiver) : receiver(receiver) {}

   static constexpr hash_id function_name{Function};
   using traits = detail::member_function<decltype(Method)>;
   using original_result = typename traits::result;
   using return_type = std::conditional_t<
       Support == support_mode::abort_op, original_result,
       std::conditional_t<std::is_void_v<original_result>, std::optional<void_call>, std::optional<original_result>>>;

   template <typename... Args>
      requires detail::call_arguments<typename traits::arguments, Args...>
   return_type operator()(Args&&... args) const {
      const auto header = call_data_header{.version = 0U, .func_name = function_name.id};
      const auto data = ::forge::raw::pack(
          std::tuple_cat(std::tuple{header}, std::tuple<std::decay_t<Args>...>{std::forward<Args>(args)...}));
      constexpr auto flags = Access == access_mode::read_only ? std::uint64_t{1} : std::uint64_t{0};
      const auto result_size = ::forge::contract::internal::call(
          receiver.value, flags, reinterpret_cast<const char*>(data.data()), data.size());
      if (result_size < 0) {
         if constexpr (Support == support_mode::abort_op) {
            check(false, "receiver does not support sync call but support_mode is set to abort_op");
         } else {
            return std::nullopt;
         }
      }
      if constexpr (std::is_void_v<original_result>) {
         if constexpr (Support == support_mode::no_op) {
            return void_call{};
         } else {
            return;
         }
      } else {
         check(static_cast<std::uint64_t>(result_size) <= std::numeric_limits<std::uint32_t>::max(),
               "sync call return value is too large");
         auto bytes = std::vector<std::uint8_t>(static_cast<std::size_t>(result_size));
         check(::forge::contract::internal::get_call_return_value(bytes.data(), bytes.size()) == bytes.size(),
               "failed to read complete sync call return value");
         auto value = ::forge::raw::unpack_exact<original_result>(bytes);
         if constexpr (Support == support_mode::no_op) {
            return std::optional<original_result>{std::move(value)};
         } else {
            return value;
         }
      }
   }

   chain::protocol::name receiver{};
};

} // namespace forge::contract
