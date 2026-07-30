module;

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

export module forge.tests.codec.json.exact_types;

export namespace forge_json_tests {

struct exact_leaf {
   std::uint32_t value = 0;

   bool operator==(const exact_leaf&) const = default;
};

struct exact_record {
   std::vector<exact_leaf> items;
   std::variant<exact_leaf, std::string> choice;
   std::optional<exact_leaf> optional;

   bool operator==(const exact_record&) const = default;
};

BOOST_DESCRIBE_STRUCT(exact_leaf, (), (value))
BOOST_DESCRIBE_STRUCT(exact_record, (), (items, choice, optional))

} // namespace forge_json_tests
