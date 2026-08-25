module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.elastic_limit_parameters;

import forge.chain.protocol.ratio;
import forge.raw.codec;

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

template <typename Stream> void raw_pack(Stream& stream, const elastic_limit_parameters& value) {
   forge::raw::pack(stream, value.target);
   forge::raw::pack(stream, value.max);
   forge::raw::pack(stream, value.periods);
   forge::raw::pack(stream, value.max_multiplier);
   forge::raw::pack(stream, value.contract_rate);
   forge::raw::pack(stream, value.expand_rate);
}

template <typename Stream> void raw_unpack(Stream& stream, elastic_limit_parameters& value) {
   forge::raw::unpack(stream, value.target);
   forge::raw::unpack(stream, value.max);
   forge::raw::unpack(stream, value.periods);
   forge::raw::unpack(stream, value.max_multiplier);
   forge::raw::unpack(stream, value.contract_rate);
   forge::raw::unpack(stream, value.expand_rate);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(elastic_limit_parameters, (), (target, max, periods, max_multiplier, contract_rate, expand_rate))
} // namespace forge::chain::protocol
#endif
