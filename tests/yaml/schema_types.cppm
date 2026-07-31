module;

#include <boost/describe.hpp>

#include <cstdint>

export module forge.tests.codec.yaml.schema_types;

export namespace forge_yaml_tests {

struct nested_limits {
   std::uint32_t deadline_ms = 0;
};

struct nested_config {
   nested_limits limits;
};

BOOST_DESCRIBE_STRUCT(nested_limits, (), (deadline_ms))
BOOST_DESCRIBE_STRUCT(nested_config, (), (limits))

} // namespace forge_yaml_tests
