module;

#include <cstdint>

export module forge.contract.system;

export import forge.chain.protocol.time;
export import forge.contract.fixed_bytes;

import forge.contract.intrinsics;

export namespace forge::contract {

using block_num_t = std::uint32_t;

[[noreturn]] void eosio_exit(std::int32_t code);
[[nodiscard]] chain::protocol::time_point current_time_point();
[[nodiscard]] chain::protocol::block_timestamp current_block_time();
[[nodiscard]] block_num_t current_block_number();
[[nodiscard]] bool is_feature_activated(const checksum256& digest);
[[nodiscard]] chain::protocol::name get_sender();

} // namespace forge::contract
