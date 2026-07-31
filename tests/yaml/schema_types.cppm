module;

#include <boost/describe.hpp>

#include <cstdint>

export module forge.tests.codec.yaml.schema_types;

import forge.variant.value;

export namespace forge_yaml_tests {

struct nested_limits {
   std::uint32_t deadline_ms = 0;
};

struct nested_config {
   nested_limits limits;
};

struct long_double_config {
   long double value = 0.0L;
};

struct long_double_parent {
   long_double_config nested;
};

BOOST_DESCRIBE_STRUCT(nested_limits, (), (deadline_ms))
BOOST_DESCRIBE_STRUCT(nested_config, (), (limits))
BOOST_DESCRIBE_STRUCT(long_double_config, (), (value))
BOOST_DESCRIBE_STRUCT(long_double_parent, (), (nested))

} // namespace forge_yaml_tests

export namespace forge {

inline void to_variant(const forge_yaml_tests::long_double_config&, variant& output) {
   output = mutable_variant_object{};
}

} // namespace forge
