module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstdint>
#include <string_view>

export module forge.contract.intrinsics;

import forge.chain.protocol.values;

export namespace forge::contract {

enum return_code : std::int32_t {
   failure = -1,
   success = 0,
};

[[noreturn]] inline void abort(std::string_view message) {
   ::forge::contract::internal::eosio_assert_message(0U, message.data(), static_cast<std::uint32_t>(message.size()));
   __builtin_unreachable();
}

inline void check(bool condition, std::string_view message) {
   if (!condition) {
      abort(message);
   }
}

[[noreturn]] inline void exit(std::int32_t code) {
   ::forge::contract::internal::eosio_exit(code);
   __builtin_unreachable();
}

inline std::uint32_t action_data_size() {
   return ::forge::contract::internal::action_data_size();
}

inline std::uint32_t read_action_data(void* destination, std::uint32_t size) {
   return ::forge::contract::internal::read_action_data(destination, size);
}

inline void set_action_return_value(const void* value, std::uint32_t size) {
   ::forge::contract::internal::set_action_return_value(const_cast<void*>(value), size);
}

inline chain::protocol::name current_receiver() {
   return chain::protocol::name{::forge::contract::internal::current_receiver()};
}

} // namespace forge::contract
