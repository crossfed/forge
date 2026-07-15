module;

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module forge.contract.dispatcher;

export import forge.contract.base;
export import forge.contract.intrinsics;
export import forge.raw.codec;

export namespace forge::contract {

template <typename Contract, typename... Arguments>
void execute_action(chain::protocol::name self, chain::protocol::name first_receiver,
                    void (Contract::*method)(Arguments...)) {
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
   std::apply([&](auto&&... value) { (instance.*method)(std::forward<decltype(value)>(value)...); }, arguments);
}

} // namespace forge::contract
