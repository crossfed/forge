module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <optional>

export module forge.chain.protocol.account_resources;

export import forge.chain.protocol.account_ram_correction;
export import forge.chain.protocol.resource_limits;
export import forge.chain.protocol.resource_meter;
export import forge.chain.protocol.resource_usage;

export namespace forge::chain::protocol {

struct account_resources {
   resource_limits current_limits;
   std::optional<resource_limits> pending_limits;
   resource_usage native_usage;
   std::optional<account_ram_correction> ram_correction;
   resource_meter cpu;
   resource_meter net;
   resource_meter ram;

   bool operator==(const account_resources&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(account_resources, (),
                      (current_limits, pending_limits, native_usage, ram_correction, cpu, net, ram))
} // namespace forge::chain::protocol
#endif
