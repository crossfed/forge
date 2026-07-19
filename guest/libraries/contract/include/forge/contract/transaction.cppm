module;

#include <forge/contract/intrinsics.h>

#include <cstddef>
#include <cstdint>
#include <vector>

export module forge.contract.transaction;

export import forge.chain.protocol.transaction;
export import forge.contract.action;

import forge.contract.datastream;
import forge.contract.intrinsics;

export namespace forge::contract {

using chain::protocol::transaction_header;
using chain::protocol::transaction;

[[nodiscard]] inline transaction get_transaction() {
   const auto size = ::transaction_size();
   auto bytes = std::vector<std::uint8_t>(size);
   if (size != 0U) {
      check(::read_transaction(reinterpret_cast<char*>(bytes.data()), size) == size,
            "failed to read complete transaction");
   }
   return ::forge::raw::unpack_exact<transaction>(bytes);
}

[[nodiscard]] inline action get_action(std::uint32_t type, std::uint32_t index) {
   const auto required = ::get_action(type, index, nullptr, 0U);
   check(required > 0, "get_action size failed");
   auto bytes = std::vector<std::uint8_t>(static_cast<std::size_t>(required));
   const auto read = ::get_action(type, index, reinterpret_cast<char*>(bytes.data()), bytes.size());
   check(read == required, "get_action failed");
   return action{::forge::raw::unpack_exact<chain::protocol::action>(bytes)};
}

inline std::size_t read_transaction(char* destination, std::size_t size) {
   return ::read_transaction(destination, size);
}

[[nodiscard]] inline std::size_t transaction_size() {
   return ::transaction_size();
}

[[nodiscard]] inline std::int32_t tapos_block_num() {
   return ::tapos_block_num();
}

[[nodiscard]] inline std::int32_t tapos_block_prefix() {
   return ::tapos_block_prefix();
}

[[nodiscard]] inline std::uint32_t expiration() {
   return ::expiration();
}

inline std::int32_t get_context_free_data(std::uint32_t index, char* destination, std::size_t size) {
   return ::get_context_free_data(index, destination, size);
}

} // namespace forge::contract
