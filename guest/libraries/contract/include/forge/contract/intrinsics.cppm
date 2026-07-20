module;

#include <cstdint>
#include <string_view>

export module forge.contract.intrinsics;

import forge.chain.protocol.values;

export namespace forge::contract {

enum return_code : std::int32_t {
   failure = -1,
   success = 0,
};

[[noreturn]] void abort(std::string_view message);
void check(bool condition, std::string_view message);
[[noreturn]] void exit(std::int32_t code);
std::uint32_t action_data_size();
std::uint32_t read_action_data(void* destination, std::uint32_t size);
void set_action_return_value(const void* value, std::uint32_t size);
chain::protocol::name current_receiver();

} // namespace forge::contract
