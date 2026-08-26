module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.activated_protocol_feature;

import forge.chain.protocol.types;

export namespace forge::chain::protocol {

struct activated_protocol_feature {
   digest feature_digest;
   std::uint32_t activation_block_num = 0;

   bool operator==(const activated_protocol_feature&) const = default;
};

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(activated_protocol_feature, (), (feature_digest, activation_block_num))
} // namespace forge::chain::protocol
#endif
