module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.resource_limits;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.types;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct resource_limits {
   resource_limits_id id;
   account_name owner;
   bool pending = false;
   std::int64_t net_weight = -1;
   std::int64_t cpu_weight = -1;
   std::int64_t ram_bytes = -1;

   bool operator==(const resource_limits&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const resource_limits& value) {
   forge::raw::pack(stream, value.id);
   forge::raw::pack(stream, value.owner);
   forge::raw::pack(stream, value.pending);
   forge::raw::pack(stream, value.net_weight);
   forge::raw::pack(stream, value.cpu_weight);
   forge::raw::pack(stream, value.ram_bytes);
}

template <typename Stream> void raw_unpack(Stream& stream, resource_limits& value) {
   forge::raw::unpack(stream, value.id);
   forge::raw::unpack(stream, value.owner);
   forge::raw::unpack(stream, value.pending);
   forge::raw::unpack(stream, value.net_weight);
   forge::raw::unpack(stream, value.cpu_weight);
   forge::raw::unpack(stream, value.ram_bytes);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(resource_limits, (), (id, owner, pending, net_weight, cpu_weight, ram_bytes))
} // namespace forge::chain::protocol
#endif
