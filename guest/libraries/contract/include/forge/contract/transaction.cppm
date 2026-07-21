module;

#include <cstddef>
#include <cstdint>
#include <vector>

export module forge.contract.transaction;

export import forge.chain.protocol.transaction;
export import forge.contract.action;

import forge.contract.datastream;
import forge.contract.intrinsics;

export namespace forge::contract {

using chain::protocol::transaction;
using chain::protocol::transaction_header;

[[nodiscard]] transaction get_transaction();
[[nodiscard]] action get_action(std::uint32_t type, std::uint32_t index);
std::size_t read_transaction(char* destination, std::size_t size);
[[nodiscard]] std::size_t transaction_size();
[[nodiscard]] std::int32_t tapos_block_num();
[[nodiscard]] std::int32_t tapos_block_prefix();
[[nodiscard]] std::uint32_t expiration();
std::int32_t get_context_free_data(std::uint32_t index, char* destination, std::size_t size);

} // namespace forge::contract
