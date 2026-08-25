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
import forge.raw.codec;

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

template <typename Stream> void raw_pack(Stream& stream, const account_resources& value) {
   forge::raw::pack(stream, value.current_limits);
   forge::raw::pack(stream, value.pending_limits);
   forge::raw::pack(stream, value.native_usage);
   forge::raw::pack(stream, value.ram_correction);
   forge::raw::pack(stream, value.cpu);
   forge::raw::pack(stream, value.net);
   forge::raw::pack(stream, value.ram);
}

template <typename Stream> void raw_unpack(Stream& stream, account_resources& value) {
   forge::raw::unpack(stream, value.current_limits);
   forge::raw::unpack(stream, value.pending_limits);
   forge::raw::unpack(stream, value.native_usage);
   forge::raw::unpack(stream, value.ram_correction);
   forge::raw::unpack(stream, value.cpu);
   forge::raw::unpack(stream, value.net);
   forge::raw::unpack(stream, value.ram);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(account_resources, (),
                      (current_limits, pending_limits, native_usage, ram_correction, cpu, net, ram))
} // namespace forge::chain::protocol
#endif
