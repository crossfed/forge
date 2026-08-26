module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.resource_usage;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.types;
export import forge.chain.protocol.usage_accumulator;

export namespace forge::chain::protocol {

struct resource_usage {
   resource_usage_id id;
   account_name owner;
   usage_accumulator net_usage;
   usage_accumulator cpu_usage;
   std::uint64_t ram_usage = 0;

   bool operator==(const resource_usage&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(resource_usage, (), (id, owner, net_usage, cpu_usage, ram_usage))
} // namespace forge::chain::protocol
#endif
