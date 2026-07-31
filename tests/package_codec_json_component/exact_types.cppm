module;

#include <boost/describe.hpp>

#include <cstdint>

export module package.codec.json.exact_types;

export namespace package_codec_json {

struct record {
   std::uint32_t value = 0;
};

BOOST_DESCRIBE_STRUCT(record, (), (value))

} // namespace package_codec_json
