module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstdint>
#include <string_view>

module forge.contract.intrinsics;

namespace forge::contract {

[[noreturn]] void abort(std::string_view message) {
   internal::eosio_assert_message(0U, message.data(), static_cast<std::uint32_t>(message.size()));
   __builtin_unreachable();
}

void check(bool condition, std::string_view message) {
   if (!condition) {
      abort(message);
   }
}

[[noreturn]] void exit(std::int32_t code) {
   internal::eosio_exit(code);
   __builtin_unreachable();
}

std::uint32_t action_data_size() {
   return internal::action_data_size();
}

std::uint32_t read_action_data(void* destination, std::uint32_t size) {
   return internal::read_action_data(destination, size);
}

void set_action_return_value(const void* value, std::uint32_t size) {
   internal::set_action_return_value(static_cast<const char*>(value), size);
}

chain::protocol::name current_receiver() {
   return chain::protocol::name{internal::current_receiver()};
}

} // namespace forge::contract
