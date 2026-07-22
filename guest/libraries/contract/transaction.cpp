module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

module forge.contract.transaction;

import forge.contract.intrinsics;
import forge.raw.codec;

namespace forge::contract {

transaction get_transaction() {
   const auto size = internal::transaction_size();
   auto bytes = std::vector<std::uint8_t>(size);
   if (size != 0U) {
      check(internal::read_transaction(reinterpret_cast<char*>(bytes.data()), size) == size,
            "failed to read complete transaction");
   }
   return forge::raw::unpack_exact<transaction>(bytes);
}

action get_action(std::uint32_t type, std::uint32_t index) {
   const auto required = internal::get_action(type, index, nullptr, 0U);
   check(required > 0, "get_action size failed");
   auto bytes = std::vector<std::uint8_t>(static_cast<std::size_t>(required));
   const auto read = internal::get_action(type, index, reinterpret_cast<char*>(bytes.data()), bytes.size());
   check(read == required, "get_action failed");
   return action{forge::raw::unpack_exact<chain::protocol::action>(bytes)};
}

std::size_t read_transaction(char* destination, std::size_t size) {
   return internal::read_transaction(destination, size);
}

std::size_t transaction_size() {
   return internal::transaction_size();
}

std::int32_t tapos_block_num() {
   return internal::tapos_block_num();
}

std::int32_t tapos_block_prefix() {
   return internal::tapos_block_prefix();
}

std::uint32_t expiration() {
   return internal::expiration();
}

std::int32_t get_context_free_data(std::uint32_t index, char* destination, std::size_t size) {
   return internal::get_context_free_data(index, destination, size);
}

} // namespace forge::contract
