module;

#include <boost/describe.hpp>

#include <string>
#include <vector>

export module forge.chain.protocol.protocol_feature;

export import forge.chain.protocol.types;

import forge.raw.codec;

export namespace forge::chain::protocol {

struct protocol_feature_specification {
   std::string name;
   std::string value;

   bool operator==(const protocol_feature_specification&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const protocol_feature_specification& value) {
   forge::raw::pack(stream, value.name);
   forge::raw::pack(stream, value.value);
}

template <typename Stream> void raw_unpack(Stream& stream, protocol_feature_specification& value) {
   forge::raw::unpack(stream, value.name);
   forge::raw::unpack(stream, value.value);
}

struct protocol_feature {
   digest feature_digest;
   digest description_digest;
   std::vector<digest> dependencies;
   std::string protocol_feature_type;
   std::vector<protocol_feature_specification> specification;

   bool operator==(const protocol_feature&) const = default;
};

template <typename Stream> void raw_pack(Stream& stream, const protocol_feature& value) {
   forge::raw::pack(stream, value.feature_digest);
   forge::raw::pack(stream, value.description_digest);
   forge::raw::pack(stream, value.dependencies);
   forge::raw::pack(stream, value.protocol_feature_type);
   forge::raw::pack(stream, value.specification);
}

template <typename Stream> void raw_unpack(Stream& stream, protocol_feature& value) {
   forge::raw::unpack(stream, value.feature_digest);
   forge::raw::unpack(stream, value.description_digest);
   forge::raw::unpack(stream, value.dependencies);
   forge::raw::unpack(stream, value.protocol_feature_type);
   forge::raw::unpack(stream, value.specification);
}

} // namespace forge::chain::protocol

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(protocol_feature_specification, (), (name, value))
BOOST_DESCRIBE_STRUCT(protocol_feature, (),
                      (feature_digest, description_digest, dependencies, protocol_feature_type, specification))
} // namespace forge::chain::protocol
