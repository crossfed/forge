module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstdint>

module forge.contract.system;

namespace forge::contract {

[[noreturn]] void eosio_exit(std::int32_t code) {
   internal::eosio_exit(code);
   __builtin_unreachable();
}

chain::protocol::time_point current_time_point() {
   return chain::protocol::time_point{
       chain::protocol::microseconds{static_cast<std::int64_t>(internal::current_time())}};
}

chain::protocol::block_timestamp current_block_time() {
   return chain::protocol::block_timestamp{current_time_point()};
}

block_num_t current_block_number() {
   return internal::get_block_num();
}

bool is_feature_activated(const checksum256& digest) {
   return internal::is_feature_activated(reinterpret_cast<const capi_checksum256*>(digest.data()));
}

chain::protocol::name get_sender() {
   return chain::protocol::name{internal::get_sender()};
}

} // namespace forge::contract
