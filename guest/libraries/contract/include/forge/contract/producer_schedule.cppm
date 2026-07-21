module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

export module forge.contract.producer_schedule;

export import forge.chain.protocol.producer_authority;

import forge.contract.intrinsics;
import forge.raw.codec;

export namespace forge::contract {

using chain::protocol::block_signing_authority;
using chain::protocol::block_signing_authority_v0;
using chain::protocol::key_weight;
using chain::protocol::producer_authority;
using chain::protocol::producer_authority_schedule;
using chain::protocol::producer_key;
using chain::protocol::producer_schedule;

[[nodiscard]] bool is_valid(const block_signing_authority_v0& authority);
[[nodiscard]] std::vector<chain::protocol::name> get_active_producers();

} // namespace forge::contract
