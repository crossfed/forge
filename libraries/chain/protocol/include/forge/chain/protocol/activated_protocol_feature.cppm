module;

#if !defined(FORGE_CONTRACT_GUEST)
#include <boost/describe.hpp>
#endif

#include <cstdint>

export module forge.chain.protocol.activated_protocol_feature;

import forge.chain.protocol.types;
import forge.raw.codec;

export namespace forge::chain::protocol {

struct activated_protocol_feature {
   digest feature_digest;
   std::uint32_t activation_block_num = 0;

   bool operator==(const activated_protocol_feature&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const activated_protocol_feature& value) {
   forge::raw::pack(stream, value.feature_digest);
   forge::raw::pack(stream, value.activation_block_num);
}

template <typename Stream> void raw_unpack(Stream& stream, activated_protocol_feature& value) {
   forge::raw::unpack(stream, value.feature_digest);
   forge::raw::unpack(stream, value.activation_block_num);
}

} // namespace forge::chain::protocol

#if !defined(FORGE_CONTRACT_GUEST)
export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(activated_protocol_feature, (), (feature_digest, activation_block_num))
} // namespace forge::chain::protocol
#endif
