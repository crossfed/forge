module;

#include <boost/describe.hpp>

#include <cstdint>

export module forge.chain.protocol.activated_protocol_feature_info;

export import forge.chain.protocol.protocol_feature;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct activated_protocol_feature_info : protocol_feature {
   std::uint32_t activation_ordinal = 0;
   std::uint32_t activation_block_num = 0;

   bool operator==(const activated_protocol_feature_info&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const activated_protocol_feature_info& value) {
   forge::raw::pack(stream, value.feature_digest);
   forge::raw::pack(stream, value.activation_ordinal);
   forge::raw::pack(stream, value.activation_block_num);
   forge::raw::pack(stream, value.description_digest);
   forge::raw::pack(stream, value.dependencies);
   forge::raw::pack(stream, value.protocol_feature_type);
   forge::raw::pack(stream, value.specification);
}

template <typename Stream> void raw_unpack(Stream& stream, activated_protocol_feature_info& value) {
   forge::raw::unpack(stream, value.feature_digest);
   forge::raw::unpack(stream, value.activation_ordinal);
   forge::raw::unpack(stream, value.activation_block_num);
   forge::raw::unpack(stream, value.description_digest);
   forge::raw::unpack(stream, value.dependencies);
   forge::raw::unpack(stream, value.protocol_feature_type);
   forge::raw::unpack(stream, value.specification);
}

BOOST_DESCRIBE_STRUCT(activated_protocol_feature_info, (),
                      (feature_digest, activation_ordinal, activation_block_num, description_digest, dependencies,
                       protocol_feature_type, specification))

} // namespace forge::chain::protocol
