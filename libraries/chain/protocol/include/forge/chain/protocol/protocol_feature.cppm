module;

#include <boost/describe.hpp>

#include <string>
#include <vector>

export module forge.chain.protocol.protocol_feature;

export import forge.chain.protocol.types;

export namespace forge::chain::protocol {

struct protocol_feature_specification {
   std::string name;
   std::string value;

   bool operator==(const protocol_feature_specification&) const = default;
};

struct protocol_feature {
   digest feature_digest;
   digest description_digest;
   std::vector<digest> dependencies;
   std::string protocol_feature_type;
   std::vector<protocol_feature_specification> specification;

   bool operator==(const protocol_feature&) const = default;
};

} // namespace forge::chain::protocol

export namespace forge::chain::protocol {
BOOST_DESCRIBE_STRUCT(protocol_feature_specification, (), (name, value))
BOOST_DESCRIBE_STRUCT(protocol_feature, (),
                      (feature_digest, description_digest, dependencies, protocol_feature_type, specification))
} // namespace forge::chain::protocol
