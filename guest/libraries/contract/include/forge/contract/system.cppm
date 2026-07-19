module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstdint>

export module forge.contract.system;

export import forge.chain.protocol.time;
export import forge.contract.fixed_bytes;

import forge.contract.intrinsics;

export namespace forge::contract {

using block_num_t = std::uint32_t;

[[noreturn]] inline void eosio_exit(std::int32_t code) {
   ::forge::contract::internal::eosio_exit(code);
   __builtin_unreachable();
}

[[nodiscard]] inline chain::protocol::time_point current_time_point() {
   return chain::protocol::time_point{
       chain::protocol::microseconds{static_cast<std::int64_t>(::forge::contract::internal::current_time())}};
}

[[nodiscard]] inline chain::protocol::block_timestamp current_block_time() {
   return chain::protocol::block_timestamp{current_time_point()};
}

[[nodiscard]] inline block_num_t current_block_number() {
   return ::forge::contract::internal::get_block_num();
}

[[nodiscard]] inline bool is_feature_activated(const checksum256& digest) {
   return ::forge::contract::internal::is_feature_activated(reinterpret_cast<const capi_checksum256*>(digest.data()));
}

[[nodiscard]] inline chain::protocol::name get_sender() {
   return chain::protocol::name{::forge::contract::internal::get_sender()};
}

} // namespace forge::contract
