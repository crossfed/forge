module;

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.contract.dispatcher;

export import forge.contract;
export import forge.contract.call;
export import forge.raw.codec;

export namespace forge::contract {

namespace detail {

template <typename Contract, typename Result, typename... Arguments, typename Method>
void execute_action_impl(chain::protocol::name self, chain::protocol::name first_receiver, Method method) {
   const auto size = action_data_size();
   auto bytes = std::vector<std::uint8_t>(size);
   if (size != 0U) {
      check(read_action_data(bytes.data(), size) == size, "failed to read complete action data");
   }
   const auto empty = std::uint8_t{};
   const auto* data = bytes.empty() ? &empty : bytes.data();

   auto arguments = std::tuple<std::decay_t<Arguments>...>{};
   auto stream = forge::datastream<const std::uint8_t*>{data, bytes.size()};
   forge::raw::unpack(stream, arguments);
   check(stream.remaining() == 0U, "action data contains trailing bytes");

   auto contract_stream = typename Contract::stream_type{
       reinterpret_cast<const char*>(data),
       bytes.size(),
   };
   auto instance = Contract{self, first_receiver, contract_stream};
   if constexpr (std::is_void_v<Result>) {
      std::apply([&](auto&&... value) { (instance.*method)(std::forward<decltype(value)>(value)...); }, arguments);
   } else {
      auto result = std::apply(
          [&](auto&&... value) { return (instance.*method)(std::forward<decltype(value)>(value)...); }, arguments);
      const auto packed = forge::raw::pack(result);
      check(packed.size() <= std::numeric_limits<std::uint32_t>::max(), "action return value is too large");
      set_action_return_value(packed.data(), static_cast<std::uint32_t>(packed.size()));
   }
}

} // namespace detail

template <typename Contract, typename Result, typename... Arguments>
void execute_action(chain::protocol::name self, chain::protocol::name first_receiver,
                    Result (Contract::*method)(Arguments...)) {
   detail::execute_action_impl<Contract, Result, Arguments...>(self, first_receiver, method);
}

namespace detail {

template <typename Contract, typename Result, typename... Arguments, typename Method>
void execute_call_impl(chain::protocol::name sender, chain::protocol::name receiver, Method method) {
   const auto size = get_call_data(nullptr, 0U);
   auto bytes = std::vector<std::uint8_t>(size);
   if (size != 0U) {
      check(get_call_data(bytes.data(), size) == size, "failed to read complete sync call data");
   }
   const auto empty = std::uint8_t{};
   const auto* data = bytes.empty() ? &empty : bytes.data();
   auto stream = forge::datastream<const std::uint8_t*>{data, bytes.size()};
   auto header = call_data_header{};
   forge::raw::unpack(stream, header);
   check(header.version == 0U, "unsupported sync call data version");

   auto arguments = std::tuple<std::decay_t<Arguments>...>{};
   forge::raw::unpack(stream, arguments);
   check(stream.remaining() == 0U, "sync call data contains trailing bytes");

   auto contract_stream = typename Contract::stream_type{
       reinterpret_cast<const char*>(data),
       bytes.size(),
   };
   auto instance = Contract{receiver, sender, contract_stream};
   if constexpr (std::is_void_v<Result>) {
      std::apply([&](auto&&... value) { (instance.*method)(std::forward<decltype(value)>(value)...); }, arguments);
   } else {
      auto result = std::apply(
          [&](auto&&... value) { return (instance.*method)(std::forward<decltype(value)>(value)...); }, arguments);
      const auto packed = forge::raw::pack(result);
      check(packed.size() <= std::numeric_limits<std::uint32_t>::max(), "sync call return value is too large");
      set_call_return_value(packed.data(), static_cast<std::uint32_t>(packed.size()));
   }
}

} // namespace detail

template <typename Contract, typename Result, typename... Arguments>
void execute_call(chain::protocol::name sender, chain::protocol::name receiver,
                  Result (Contract::*method)(Arguments...)) {
   detail::execute_call_impl<Contract, Result, Arguments...>(sender, receiver, method);
}

template <typename Contract, typename Result, typename... Arguments>
void execute_call(chain::protocol::name sender, chain::protocol::name receiver,
                  Result (Contract::*method)(Arguments...) const) {
   detail::execute_call_impl<Contract, Result, Arguments...>(sender, receiver, method);
}

template <typename Contract, typename Result, typename... Arguments>
void execute_action(chain::protocol::name self, chain::protocol::name first_receiver,
                    Result (Contract::*method)(Arguments...) const) {
   detail::execute_action_impl<Contract, Result, Arguments...>(self, first_receiver, method);
}

struct dispatch_entry {
   std::uint64_t name = 0;
   void (*invoke)(chain::protocol::name, chain::protocol::name) = nullptr;
};

struct notification_entry {
   std::uint64_t code = 0;
   std::uint64_t action = 0;
   void (*invoke)(chain::protocol::name, chain::protocol::name) = nullptr;
};

struct call_entry {
   std::uint64_t id = 0;
   void (*invoke)(chain::protocol::name, chain::protocol::name) = nullptr;
};

template <typename Contract, auto Method> constexpr dispatch_entry make_dispatch_entry(std::uint64_t name) {
   return {
       name,
       [](chain::protocol::name self, chain::protocol::name first_receiver) {
          execute_action<Contract>(self, first_receiver, Method);
       },
   };
}

template <typename Contract, auto Method>
constexpr notification_entry make_notification_entry(std::uint64_t code, std::uint64_t action) {
   return {
       code,
       action,
       [](chain::protocol::name self, chain::protocol::name first_receiver) {
          execute_action<Contract>(self, first_receiver, Method);
       },
   };
}

template <typename Contract, auto Method> constexpr call_entry make_call_entry(std::uint64_t id) {
   return {
       id,
       [](chain::protocol::name sender, chain::protocol::name receiver) {
          execute_call<Contract>(sender, receiver, Method);
       },
   };
}

namespace detail {

template <typename Deferred, typename Contract, auto Method>
constexpr dispatch_entry make_deferred_dispatch_entry(std::uint64_t name) {
   static_cast<void>(sizeof(Deferred*));
   return make_dispatch_entry<Contract, Method>(name);
}

} // namespace detail

void dispatch(chain::protocol::name receiver, chain::protocol::name code, std::uint64_t action,
              const dispatch_entry* entries, std::size_t size, const notification_entry* notifications = nullptr,
              std::size_t notification_size = 0U);

void dispatch_call(chain::protocol::name sender, chain::protocol::name receiver, const call_entry* entries,
                   std::size_t size);

template <std::size_t Size>
void dispatch(chain::protocol::name receiver, chain::protocol::name code, std::uint64_t action,
              const dispatch_entry (&entries)[Size]) {
   dispatch(receiver, code, action, entries, Size, nullptr, 0U);
}


template <std::size_t ActionSize, std::size_t NotificationSize>
void dispatch(chain::protocol::name receiver, chain::protocol::name code, std::uint64_t action,
              const dispatch_entry (&entries)[ActionSize], const notification_entry (&notifications)[NotificationSize]) {
   dispatch(receiver, code, action, entries, ActionSize, notifications, NotificationSize);
}

inline void dispatch(chain::protocol::name receiver, chain::protocol::name code, std::uint64_t action) {
   dispatch(receiver, code, action, nullptr, 0U, nullptr, 0U);
}

} // namespace forge::contract
