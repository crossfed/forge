module;

#include <boost/describe.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
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

struct exact_map_record {
   std::map<std::string, exact_leaf> values;
};

struct exact_set_record {
   std::set<std::string> ordered;
   std::unordered_set<std::string> unordered;
};

BOOST_DESCRIBE_STRUCT(exact_leaf, (), (value))
BOOST_DESCRIBE_STRUCT(exact_record, (), (items, choice, optional))
BOOST_DESCRIBE_STRUCT(exact_map_record, (), (values))
BOOST_DESCRIBE_STRUCT(exact_set_record, (), (ordered, unordered))

} // namespace forge_json_tests
