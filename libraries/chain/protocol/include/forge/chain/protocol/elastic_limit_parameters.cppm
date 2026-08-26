module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.elastic_limit_parameters;

import forge.chain.protocol.ratio;

export namespace forge::chain::protocol {

struct elastic_limit_parameters {
   std::uint64_t target = 0;
   std::uint64_t max = 0;
   std::uint32_t periods = 0;
   std::uint32_t max_multiplier = 0;
   ratio contract_rate;
   ratio expand_rate;

   bool operator==(const elastic_limit_parameters&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(elastic_limit_parameters, (), (target, max, periods, max_multiplier, contract_rate, expand_rate))
} // namespace forge::chain::protocol
#endif
