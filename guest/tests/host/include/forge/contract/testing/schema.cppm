module;

#include <boost/describe.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

export module forge.contract.testing.schema;

import forge.chain.protocol.fixed_key;
import forge.db.object.index;
import forge.db.object.object;

export namespace forge::contract::testing {

inline constexpr std::uint8_t schema_space = 250;

struct float64 {
   std::uint64_t bits = 0;

   bool operator==(const float64&) const = default;
};

BOOST_DESCRIBE_STRUCT(float64, (), (bits))

struct float128 {
   std::array<std::uint64_t, 2> words{};

   bool operator==(const float128&) const = default;
};

BOOST_DESCRIBE_STRUCT(float128, (), (words))

using uint256 = forge::chain::protocol::key256;

struct table : forge::db::object::object<table, schema_space, 1> {
   std::uint64_t code = 0;
   std::uint64_t scope = 0;
   std::uint64_t table_name = 0;
   std::uint64_t payer = 0;
   std::uint32_t count = 0;

   BOOST_DESCRIBE_CLASS(table, (object_base_type), (code, scope, table_name, payer, count), (), ())
};

struct key_value : forge::db::object::object<key_value, schema_space, 2> {
   table::id_t table_id;
   std::uint64_t primary = 0;
   std::uint64_t payer = 0;
   std::vector<std::uint8_t> value;

   BOOST_DESCRIBE_CLASS(key_value, (object_base_type), (table_id, primary, payer, value), (), ())
};

struct index64 : forge::db::object::object<index64, schema_space, 3> {
   table::id_t table_id;
   std::uint64_t primary = 0;
   std::uint64_t payer = 0;
   std::uint64_t secondary = 0;

   BOOST_DESCRIBE_CLASS(index64, (object_base_type), (table_id, primary, payer, secondary), (), ())
};

struct index128 : forge::db::object::object<index128, schema_space, 4> {
   table::id_t table_id;
   std::uint64_t primary = 0;
   std::uint64_t payer = 0;
   unsigned __int128 secondary = 0;

   BOOST_DESCRIBE_CLASS(index128, (object_base_type), (table_id, primary, payer, secondary), (), ())
};

struct index256 : forge::db::object::object<index256, schema_space, 5> {
   table::id_t table_id;
   std::uint64_t primary = 0;
   std::uint64_t payer = 0;
   uint256 secondary{};

   BOOST_DESCRIBE_CLASS(index256, (object_base_type), (table_id, primary, payer, secondary), (), ())
};

struct index_double : forge::db::object::object<index_double, schema_space, 6> {
   table::id_t table_id;
   std::uint64_t primary = 0;
   std::uint64_t payer = 0;
   float64 secondary{};

   BOOST_DESCRIBE_CLASS(index_double, (object_base_type), (table_id, primary, payer, secondary), (), ())
};

struct index_long_double : forge::db::object::object<index_long_double, schema_space, 7> {
   table::id_t table_id;
   std::uint64_t primary = 0;
   std::uint64_t payer = 0;
   float128 secondary{};

   BOOST_DESCRIBE_CLASS(index_long_double, (object_base_type), (table_id, primary, payer, secondary), (), ())
};

} // namespace forge::contract::testing

export namespace forge::db::object {

template <> struct sort_key<forge::contract::testing::float64> {
   [[nodiscard]] sort_key_bytes operator()(forge::contract::testing::float64 value) const {
      auto bits = value.bits;
      const auto magnitude = bits & 0x7fff'ffff'ffff'ffffULL;
      if (magnitude > 0x7ff0'0000'0000'0000ULL) {
         throw std::domain_error{"NaN is not an ordered database key"};
      }
      if (magnitude == 0U) {
         bits = 0U;
      }
      bits = (bits & 0x8000'0000'0000'0000ULL) != 0U ? ~bits : bits ^ 0x8000'0000'0000'0000ULL;
      return sort_key<std::uint64_t>{}(bits);
   }
};

template <> struct sort_key<forge::contract::testing::float128> {
   [[nodiscard]] sort_key_bytes operator()(forge::contract::testing::float128 value) const {
      auto low = value.words[0];
      auto high = value.words[1];
      const auto exponent = high & 0x7fff'0000'0000'0000ULL;
      const auto fraction = (high & 0x0000'ffff'ffff'ffffULL) | low;
      if (exponent == 0x7fff'0000'0000'0000ULL && fraction != 0U) {
         throw std::domain_error{"NaN is not an ordered database key"};
      }
      if (exponent == 0U && fraction == 0U) {
         high = 0U;
         low = 0U;
      }
      if ((high & 0x8000'0000'0000'0000ULL) != 0U) {
         high = ~high;
         low = ~low;
      } else {
         high ^= 0x8000'0000'0000'0000ULL;
      }
      auto result = sort_key<std::uint64_t>{}(high);
      const auto tail = sort_key<std::uint64_t>{}(low);
      result.insert(result.end(), tail.begin(), tail.end());
      return result;
   }
};

} // namespace forge::db::object

export namespace forge::contract::testing {

struct by_id;
struct by_code_scope_table;
struct by_scope_primary;
struct by_primary;
struct by_secondary;

using table_index = forge::db::object::object_index<
    table,
    forge::db::object::indexed_by<forge::db::object::ranked_primary_unique<by_id, forge::db::object::ranked_schema<1>>,
                                  forge::db::object::ranked_unique<
                                      by_code_scope_table,
                                      forge::db::object::composite_key<forge::db::object::member<&table::code>,
                                                                       forge::db::object::member<&table::scope>,
                                                                       forge::db::object::member<&table::table_name>>,
                                      forge::db::object::ranked_schema<1>>>>;

using key_value_index = forge::db::object::object_index<
    key_value,
    forge::db::object::indexed_by<forge::db::object::ranked_primary_unique<by_id, forge::db::object::ranked_schema<1>>,
                                  forge::db::object::ranked_unique<
                                      by_scope_primary,
                                      forge::db::object::composite_key<forge::db::object::member<&key_value::table_id>,
                                                                       forge::db::object::member<&key_value::primary>>,
                                      forge::db::object::ranked_schema<1>>>>;

template <typename Row>
using secondary_index = forge::db::object::object_index<
    Row,
    forge::db::object::indexed_by<
        forge::db::object::ranked_primary_unique<by_id, forge::db::object::ranked_schema<1>>,
        forge::db::object::ranked_unique<by_primary,
                                         forge::db::object::composite_key<forge::db::object::member<&Row::table_id>,
                                                                          forge::db::object::member<&Row::primary>>,
                                         forge::db::object::ranked_schema<1>>,
        forge::db::object::ranked_unique<by_secondary,
                                         forge::db::object::composite_key<forge::db::object::member<&Row::table_id>,
                                                                          forge::db::object::member<&Row::secondary>,
                                                                          forge::db::object::member<&Row::primary>>,
                                         forge::db::object::ranked_schema<1>>>>;

using index64_index = secondary_index<index64>;
using index128_index = secondary_index<index128>;
using index256_index = secondary_index<index256>;
using index_double_index = secondary_index<index_double>;
using index_long_double_index = secondary_index<index_long_double>;

} // namespace forge::contract::testing

export namespace forge::db::object {

template <> struct index_for_id<forge::contract::testing::table::id_t> {
   using type = forge::contract::testing::table_index;
};

template <> struct index_for_id<forge::contract::testing::key_value::id_t> {
   using type = forge::contract::testing::key_value_index;
};

template <> struct index_for_id<forge::contract::testing::index64::id_t> {
   using type = forge::contract::testing::index64_index;
};

template <> struct index_for_id<forge::contract::testing::index128::id_t> {
   using type = forge::contract::testing::index128_index;
};

template <> struct index_for_id<forge::contract::testing::index256::id_t> {
   using type = forge::contract::testing::index256_index;
};

template <> struct index_for_id<forge::contract::testing::index_double::id_t> {
   using type = forge::contract::testing::index_double_index;
};

template <> struct index_for_id<forge::contract::testing::index_long_double::id_t> {
   using type = forge::contract::testing::index_long_double_index;
};

} // namespace forge::db::object
