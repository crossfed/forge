module;

#include <cstdint>
#include <optional>
#include <vector>

export module forge.contract.privileged;

export import forge.chain.protocol.blockchain_parameters;
export import forge.chain.protocol.kv_parameters;
export import forge.contract.fixed_bytes;
export import forge.contract.producer_schedule;

import forge.contract.datastream;
import forge.contract.intrinsics;

export namespace forge::contract {

using chain::protocol::blockchain_parameters;
using chain::protocol::kv_parameters;

void set_blockchain_parameters(const blockchain_parameters& parameters);
void get_blockchain_parameters(blockchain_parameters& parameters);
void set_kv_parameters(const kv_parameters& parameters);
void get_resource_limits(chain::protocol::name account, std::int64_t& ram_bytes, std::int64_t& net_weight,
                         std::int64_t& cpu_weight);
void set_resource_limits(chain::protocol::name account, std::int64_t ram_bytes, std::int64_t net_weight,
                         std::int64_t cpu_weight);
[[nodiscard]] std::optional<std::uint64_t> set_proposed_producers(const std::vector<producer_key>& producers);
[[nodiscard]] std::optional<std::uint64_t> set_proposed_producers(const std::vector<producer_authority>& producers);
[[nodiscard]] bool is_privileged(chain::protocol::name account);
void set_privileged(chain::protocol::name account, bool privileged);
void preactivate_feature(const checksum256& digest);

} // namespace forge::contract
