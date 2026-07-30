module;

#include <boost/describe.hpp>
#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <cstdint>
#include <map>
#include <memory>
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

struct exact_alias_leaf {
   std::uint32_t bind_port = 0;
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

struct exact_pointer_record {
   std::shared_ptr<exact_alias_leaf> shared;
   std::unique_ptr<exact_alias_leaf> unique;
};

struct exact_set_record {
   std::set<std::string> ordered;
   std::unordered_set<std::string> unordered;
};

using exact_multi_index =
    boost::multi_index_container<exact_leaf,
                                 boost::multi_index::indexed_by<boost::multi_index::ordered_unique<
                                     boost::multi_index::member<exact_leaf, std::uint32_t, &exact_leaf::value>>>>;

struct exact_multi_index_record {
   exact_multi_index values;
};

BOOST_DESCRIBE_STRUCT(exact_leaf, (), (value))
BOOST_DESCRIBE_STRUCT(exact_alias_leaf, (), (bind_port))
BOOST_DESCRIBE_STRUCT(exact_record, (), (items, choice, optional))
BOOST_DESCRIBE_STRUCT(exact_map_record, (), (values))
BOOST_DESCRIBE_STRUCT(exact_pointer_record, (), (shared, unique))
BOOST_DESCRIBE_STRUCT(exact_set_record, (), (ordered, unordered))
BOOST_DESCRIBE_STRUCT(exact_multi_index_record, (), (values))

} // namespace forge_json_tests
