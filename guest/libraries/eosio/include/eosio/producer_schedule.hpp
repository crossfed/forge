#pragma once

#include <eosio/crypto.hpp>
#include <eosio/name.hpp>

#include <cstdint>
#include <variant>
#include <vector>

import forge.contract.producer_schedule;
import forge.raw.codec;

namespace eosio {

using forge::chain::protocol::key_weight;
using forge::chain::protocol::producer_authority_schedule;
using forge::contract::get_active_producers;

struct producer_key {
   name producer_name;
   public_key block_signing_key;

   friend constexpr bool operator<(const producer_key& left, const producer_key& right) {
      return left.producer_name < right.producer_name;
   }
};

struct producer_schedule {
   std::uint32_t version = 0;
   std::vector<producer_key> producers;
};

struct block_signing_authority_v0 {
   std::uint32_t threshold = 0;
   std::vector<key_weight> keys;
};

using block_signing_authority = std::variant<block_signing_authority_v0>;

struct producer_authority {
   name producer_name;
   block_signing_authority authority;
};

[[nodiscard]] inline forge::chain::protocol::producer_key to_protocol(const producer_key& value) {
   return {value.producer_name, value.block_signing_key};
}

[[nodiscard]] inline forge::chain::protocol::producer_authority to_protocol(const producer_authority& value) {
   const auto& authority = std::get<block_signing_authority_v0>(value.authority);
   return {value.producer_name,
           forge::chain::protocol::block_signing_authority{
               forge::chain::protocol::block_signing_authority_v0{authority.threshold, authority.keys}}};
}

} // namespace eosio
