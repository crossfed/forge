module;

#include <forge/contract/intrinsics.hpp>

#include <cstdint>
#include <string_view>

export module forge.contract.intrinsics;

export namespace forge::contract {

[[noreturn]] inline void abort(std::string_view message) {
   intrinsic::eosio_assert_message(0U, message.data(), static_cast<std::uint32_t>(message.size()));
   __builtin_unreachable();
}

inline void check(bool condition, std::string_view message) {
   if (!condition) {
      abort(message);
   }
}

[[noreturn]] inline void exit(std::int32_t code) {
   intrinsic::eosio_exit(code);
   __builtin_unreachable();
}

inline std::uint32_t action_data_size() {
   return intrinsic::action_data_size();
}

inline std::uint32_t read_action_data(void* destination, std::uint32_t size) {
   return intrinsic::read_action_data(destination, size);
}

} // namespace forge::contract
