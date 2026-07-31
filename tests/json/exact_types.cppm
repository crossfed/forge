module;

#include <boost/describe.hpp>
#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

export module forge.tests.codec.json.exact_types;

export import forge.chain.protocol.fixed_key;
export import forge.crypto.digest.sha256;
export import forge.raw.varint;
import forge.variant.value;

export namespace forge_json_tests {

struct exact_leaf {
   std::uint32_t value = 0;

   bool operator==(const exact_leaf&) const = default;
};

struct exact_alias_leaf {
   std::uint32_t bind_port = 0;

   auto operator<=>(const exact_alias_leaf&) const = default;
};

struct exact_scalar_record {
   bool enabled = false;
   std::int8_t signed_value = 0;
   std::uint8_t unsigned_value = 0;
   float ratio = 0.0F;
   std::string label;
};

struct exact_double_record {
   double value = 0.0;
};

struct exact_long_double_record {
   long double value = 0.0L;
};

struct exact_dotted_leaf {
   std::uint32_t deadline_ms = 0;

   auto operator<=>(const exact_dotted_leaf&) const = default;
};

struct exact_dotted_parent {
   exact_dotted_leaf config;
};

struct exact_dotted_schema_parent {
   exact_dotted_leaf config;
};

struct exact_dotted_pair_parent {
   std::pair<exact_dotted_leaf, std::uint32_t> value;
};

struct exact_dotted_map_key_parent {
   std::map<exact_dotted_leaf, std::uint32_t> values;
};

struct exact_dotted_variant_parent {
   std::variant<exact_dotted_leaf, std::string> value;
};

struct exact_wide_integer_record {
   __int128 signed_value = 0;
   unsigned __int128 unsigned_value = 0;
};

struct exact_varint_record {
   forge::signed_int signed_value;
   forge::unsigned_int unsigned_value;
};

struct exact_chrono_record {
   std::chrono::microseconds delay{};
   std::chrono::sys_time<std::chrono::microseconds> timestamp{};
};

struct exact_blob_record {
   forge::blob payload;
};

struct exact_byte_vector_record {
   std::vector<char> payload;
};

struct exact_digest_record {
   forge::crypto::digest::sha256 value;
};

struct exact_fixed_key_record {
   forge::chain::protocol::key256 value;
};

enum class exact_path_policy {
   direct_only,
   direct_preferred,
};

struct exact_enum_record {
   exact_path_policy policy = exact_path_policy::direct_only;
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

struct exact_schema_set_record {
   std::set<exact_alias_leaf> values;
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
BOOST_DESCRIBE_STRUCT(exact_scalar_record, (), (enabled, signed_value, unsigned_value, ratio, label))
BOOST_DESCRIBE_STRUCT(exact_double_record, (), (value))
BOOST_DESCRIBE_STRUCT(exact_dotted_leaf, (), (deadline_ms))
BOOST_DESCRIBE_STRUCT(exact_dotted_parent, (), (config))
BOOST_DESCRIBE_STRUCT(exact_dotted_schema_parent, (), (config))
BOOST_DESCRIBE_STRUCT(exact_dotted_pair_parent, (), (value))
BOOST_DESCRIBE_STRUCT(exact_dotted_map_key_parent, (), (values))
BOOST_DESCRIBE_STRUCT(exact_dotted_variant_parent, (), (value))
BOOST_DESCRIBE_STRUCT(exact_wide_integer_record, (), (signed_value, unsigned_value))
BOOST_DESCRIBE_STRUCT(exact_varint_record, (), (signed_value, unsigned_value))
BOOST_DESCRIBE_STRUCT(exact_chrono_record, (), (delay, timestamp))
BOOST_DESCRIBE_STRUCT(exact_blob_record, (), (payload))
BOOST_DESCRIBE_STRUCT(exact_byte_vector_record, (), (payload))
BOOST_DESCRIBE_STRUCT(exact_digest_record, (), (value))
BOOST_DESCRIBE_STRUCT(exact_fixed_key_record, (), (value))
BOOST_DESCRIBE_ENUM(exact_path_policy, direct_only, direct_preferred)
BOOST_DESCRIBE_STRUCT(exact_enum_record, (), (policy))
BOOST_DESCRIBE_STRUCT(exact_record, (), (items, choice, optional))
BOOST_DESCRIBE_STRUCT(exact_map_record, (), (values))
BOOST_DESCRIBE_STRUCT(exact_pointer_record, (), (shared, unique))
BOOST_DESCRIBE_STRUCT(exact_schema_set_record, (), (values))
BOOST_DESCRIBE_STRUCT(exact_set_record, (), (ordered, unordered))
BOOST_DESCRIBE_STRUCT(exact_multi_index_record, (), (values))

} // namespace forge_json_tests
