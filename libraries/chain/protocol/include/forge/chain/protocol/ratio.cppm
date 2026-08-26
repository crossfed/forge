module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.ratio;

export namespace forge::chain::protocol {

struct ratio {
   std::uint64_t numerator = 0;
   std::uint64_t denominator = 1;

   bool operator==(const ratio&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(ratio, (), (numerator, denominator))
} // namespace forge::chain::protocol
#endif
