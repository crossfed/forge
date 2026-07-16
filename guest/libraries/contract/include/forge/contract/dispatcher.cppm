module;

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.contract.dispatcher;

export import forge.contract;
export import forge.raw.codec;

export namespace forge::contract {

template <typename Contract, typename Result, typename... Arguments>
void execute_action(chain::protocol::name self, chain::protocol::name first_receiver,
                    Result (Contract::*method)(Arguments...)) {
   const auto size = action_data_size();
   auto bytes = std::vector<std::uint8_t>(size);
   if (size != 0U) {
      check(read_action_data(bytes.data(), size) == size, "failed to read complete action data");
   }

   auto arguments = std::tuple<std::decay_t<Arguments>...>{};
   auto stream = forge::datastream<const std::uint8_t*>{bytes.data(), bytes.size()};
   forge::raw::unpack(stream, arguments);
   check(stream.remaining() == 0U, "action data contains trailing bytes");

   auto contract_stream = typename Contract::stream_type{
       reinterpret_cast<const char*>(bytes.data()),
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

struct dispatch_entry {
   std::uint64_t name = 0;
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

void dispatch(chain::protocol::name receiver, chain::protocol::name code, std::uint64_t action,
              const dispatch_entry* entries, std::size_t size);

template <std::size_t Size>
void dispatch(chain::protocol::name receiver, chain::protocol::name code, std::uint64_t action,
              const dispatch_entry (&entries)[Size]) {
   dispatch(receiver, code, action, entries, Size);
}

inline void dispatch(chain::protocol::name receiver, chain::protocol::name code, std::uint64_t action) {
   dispatch(receiver, code, action, nullptr, 0U);
}

} // namespace forge::contract
