module;

#include <forge/contract/internal/intrinsics.hpp>

#include <cstdint>
#include <optional>
#include <vector>

module forge.contract.privileged;

import forge.contract.intrinsics;
import forge.raw.codec;

namespace forge::contract {

void set_blockchain_parameters(const blockchain_parameters& parameters) {
   const auto bytes = forge::raw::pack(parameters);
   internal::set_blockchain_parameters_packed(reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data())),
                                              bytes.size());
}

void get_blockchain_parameters(blockchain_parameters& parameters) {
   auto bytes = std::vector<std::uint8_t>(internal::get_blockchain_parameters_packed(nullptr, 0U));
   if (!bytes.empty()) {
      const auto size = internal::get_blockchain_parameters_packed(reinterpret_cast<char*>(bytes.data()), bytes.size());
      check(size <= bytes.size(), "blockchain parameter buffer is too small");
      bytes.resize(size);
   }
   parameters = forge::raw::unpack_exact<blockchain_parameters>(bytes);
}

void set_kv_parameters(const kv_parameters& parameters) {
   auto bytes = forge::raw::pack(std::uint32_t{0});
   const auto packed_parameters = forge::raw::pack(parameters);
   bytes.insert(bytes.end(), packed_parameters.begin(), packed_parameters.end());
   internal::set_kv_parameters_packed(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void get_resource_limits(chain::protocol::name account, std::int64_t& ram_bytes, std::int64_t& net_weight,
                         std::int64_t& cpu_weight) {
   internal::get_resource_limits(account.value, &ram_bytes, &net_weight, &cpu_weight);
}

void set_resource_limits(chain::protocol::name account, std::int64_t ram_bytes, std::int64_t net_weight,
                         std::int64_t cpu_weight) {
   internal::set_resource_limits(account.value, ram_bytes, net_weight, cpu_weight);
}

std::optional<std::uint64_t> set_proposed_producers(const std::vector<producer_key>& producers) {
   const auto bytes = forge::raw::pack(producers);
   const auto version =
       internal::set_proposed_producers(reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data())), bytes.size());
   return version < 0 ? std::nullopt : std::optional<std::uint64_t>{static_cast<std::uint64_t>(version)};
}

std::optional<std::uint64_t> set_proposed_producers(const std::vector<producer_authority>& producers) {
   const auto bytes = forge::raw::pack(producers);
   const auto version = internal::set_proposed_producers_ex(
       1U, reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data())), bytes.size());
   return version < 0 ? std::nullopt : std::optional<std::uint64_t>{static_cast<std::uint64_t>(version)};
}

bool is_privileged(chain::protocol::name account) {
   return internal::is_privileged(account.value);
}

void set_privileged(chain::protocol::name account, bool privileged) {
   internal::set_privileged(account.value, privileged);
}

void preactivate_feature(const checksum256& digest) {
   internal::preactivate_feature(reinterpret_cast<const capi_checksum256*>(digest.data()));
}

} // namespace forge::contract
