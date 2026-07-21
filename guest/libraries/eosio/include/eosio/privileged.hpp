#pragma once

#include <eosio/fixed_bytes.hpp>
#include <eosio/producer_schedule.hpp>

#include <cstdint>
#include <optional>
#include <vector>

import forge.contract.privileged;

namespace eosio {

using forge::contract::blockchain_parameters;
using forge::contract::get_blockchain_parameters;
using forge::contract::get_resource_limits;
using forge::contract::is_privileged;
using forge::contract::kv_parameters;
using forge::contract::set_blockchain_parameters;
using forge::contract::set_kv_parameters;
using forge::contract::set_privileged;
using forge::contract::set_resource_limits;

[[nodiscard]] inline std::optional<std::uint64_t> set_proposed_producers(const std::vector<producer_key>& producers) {
   auto values = std::vector<forge::chain::protocol::producer_key>{};
   values.reserve(producers.size());
   for (const auto& producer : producers) {
      values.push_back(to_protocol(producer));
   }
   return forge::contract::set_proposed_producers(values);
}

[[nodiscard]] inline std::optional<std::uint64_t>
set_proposed_producers(const std::vector<producer_authority>& producers) {
   auto values = std::vector<forge::chain::protocol::producer_authority>{};
   values.reserve(producers.size());
   for (const auto& producer : producers) {
      values.push_back(to_protocol(producer));
   }
   return forge::contract::set_proposed_producers(values);
}

inline void preactivate_feature(const checksum256& digest) {
   forge::contract::preactivate_feature(detail::to_digest<forge::contract::checksum256>(digest));
}

} // namespace eosio

// EOSIO declared these records in namespace eosio, so unqualified calls found
// the helpers through argument-dependent lookup. The Forge veneer aliases the
// records to their canonical protocol types; these using-declarations preserve
// that source behavior without moving runtime operations into protocol.
using eosio::set_blockchain_parameters;
using eosio::set_kv_parameters;
