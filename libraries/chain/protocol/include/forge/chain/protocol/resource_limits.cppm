module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.resource_limits;

export import forge.chain.protocol.native_ids;
export import forge.chain.protocol.types;

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

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(resource_limits, (), (id, owner, pending, net_weight, cpu_weight, ram_bytes))
} // namespace forge::chain::protocol
#endif
