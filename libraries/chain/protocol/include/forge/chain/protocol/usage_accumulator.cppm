module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.usage_accumulator;

export namespace forge::chain::protocol {

struct usage_accumulator {
   std::uint32_t last_ordinal = 0;
   std::uint64_t value_ex = 0;
   std::uint64_t consumed = 0;

   bool operator==(const usage_accumulator&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(usage_accumulator, (), (last_ordinal, value_ex, consumed))
} // namespace forge::chain::protocol
#endif
