module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.resource_limits_config;

export import forge.chain.protocol.elastic_limit_parameters;
export import forge.chain.protocol.native_ids;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct resource_limits_config {
   resource_config_id id;
   elastic_limit_parameters cpu_limit_parameters{
       .target = 20'000U,
       .max = 200'000U,
       .periods = 120U,
       .max_multiplier = 1'000U,
       .contract_rate = {.numerator = 99U, .denominator = 100U},
       .expand_rate = {.numerator = 1'000U, .denominator = 999U},
   };
   elastic_limit_parameters net_limit_parameters{
       .target = 104'857U,
       .max = 1'048'576U,
       .periods = 120U,
       .max_multiplier = 1'000U,
       .contract_rate = {.numerator = 99U, .denominator = 100U},
       .expand_rate = {.numerator = 1'000U, .denominator = 999U},
   };
   std::uint32_t account_cpu_usage_average_window = 172'800U;
   std::uint32_t account_net_usage_average_window = 172'800U;

   bool operator==(const resource_limits_config&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const resource_limits_config& value) {
   forge::raw::pack(stream, value.id);
   forge::raw::pack(stream, value.cpu_limit_parameters);
   forge::raw::pack(stream, value.net_limit_parameters);
   forge::raw::pack(stream, value.account_cpu_usage_average_window);
   forge::raw::pack(stream, value.account_net_usage_average_window);
}

template <typename Stream> void raw_unpack(Stream& stream, resource_limits_config& value) {
   forge::raw::unpack(stream, value.id);
   forge::raw::unpack(stream, value.cpu_limit_parameters);
   forge::raw::unpack(stream, value.net_limit_parameters);
   forge::raw::unpack(stream, value.account_cpu_usage_average_window);
   forge::raw::unpack(stream, value.account_net_usage_average_window);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(resource_limits_config, (),
                      (id, cpu_limit_parameters, net_limit_parameters, account_cpu_usage_average_window,
                       account_net_usage_average_window))
} // namespace forge::chain::protocol
#endif
