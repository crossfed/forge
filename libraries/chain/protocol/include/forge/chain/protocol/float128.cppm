module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

export module forge.chain.protocol.float128;

export import :value;

#if !defined(FORGE_CONTRACT_GUEST)
export import :ordered;
export import :variant;
#endif

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(float128, (), (bits))
} // namespace forge::chain::protocol
#endif
