module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.resource_limits_state;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.usage_accumulator;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct resource_limits_state {
   resource_state_id id;
   usage_accumulator average_block_net_usage;
   usage_accumulator average_block_cpu_usage;
   std::uint64_t pending_net_usage = 0;
   std::uint64_t pending_cpu_usage = 0;
   std::uint64_t total_net_weight = 0;
   std::uint64_t total_cpu_weight = 0;
   std::uint64_t total_ram_bytes = 0;
   std::uint64_t virtual_net_limit = 0;
   std::uint64_t virtual_cpu_limit = 0;

   bool operator==(const resource_limits_state&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const resource_limits_state& value) {
   forge::raw::pack(stream, value.id);
   forge::raw::pack(stream, value.average_block_net_usage);
   forge::raw::pack(stream, value.average_block_cpu_usage);
   forge::raw::pack(stream, value.pending_net_usage);
   forge::raw::pack(stream, value.pending_cpu_usage);
   forge::raw::pack(stream, value.total_net_weight);
   forge::raw::pack(stream, value.total_cpu_weight);
   forge::raw::pack(stream, value.total_ram_bytes);
   forge::raw::pack(stream, value.virtual_net_limit);
   forge::raw::pack(stream, value.virtual_cpu_limit);
}

template <typename Stream> void raw_unpack(Stream& stream, resource_limits_state& value) {
   forge::raw::unpack(stream, value.id);
   forge::raw::unpack(stream, value.average_block_net_usage);
   forge::raw::unpack(stream, value.average_block_cpu_usage);
   forge::raw::unpack(stream, value.pending_net_usage);
   forge::raw::unpack(stream, value.pending_cpu_usage);
   forge::raw::unpack(stream, value.total_net_weight);
   forge::raw::unpack(stream, value.total_cpu_weight);
   forge::raw::unpack(stream, value.total_ram_bytes);
   forge::raw::unpack(stream, value.virtual_net_limit);
   forge::raw::unpack(stream, value.virtual_cpu_limit);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(resource_limits_state, (),
                      (id, average_block_net_usage, average_block_cpu_usage, pending_net_usage, pending_cpu_usage,
                       total_net_weight, total_cpu_weight, total_ram_bytes, virtual_net_limit, virtual_cpu_limit))
} // namespace forge::chain::protocol
#endif
