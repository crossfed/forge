#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/describe.hpp>
#include <forge/db/object/macros.hpp>

import forge.db.ids.object_id;
import forge.db.core.driver;
import forge.db.core.record;
import forge.db.object.header;
import forge.db.object.index;
import forge.db.object.object;
import forge.db.object.snapshot;
import forge.db.object.store;

struct external_key {
   std::uint32_t value = 0;
};

BOOST_DESCRIBE_STRUCT(external_key, (), (value))

template <> struct forge::db::object::sort_key<external_key> {
   forge::db::object::sort_key_bytes operator()(const external_key& key) const {
      return {
          static_cast<std::byte>((key.value >> 24U) & 0xffU),
          static_cast<std::byte>((key.value >> 16U) & 0xffU),
          static_cast<std::byte>((key.value >> 8U) & 0xffU),
          static_cast<std::byte>(key.value & 0xffU),
      };
   }
};

struct account : forge::db::object::object<account, 1, 7> {
   std::string name;
   external_key key;
   std::uint64_t usage = 0;
};

BOOST_DESCRIBE_STRUCT(account, (forge::db::object::object<account, 1, 7>), (name, key, usage))

struct by_id;
struct by_name;
struct by_key;
struct by_usage;

using account_object = forge::db::object::object_index<
    account,
    forge::db::object::indexed_by<forge::db::object::ranked_primary_unique<
                                     by_id,
                                     forge::db::object::ranked_schema<1>,
                                     forge::db::object::sum<
                                        by_usage, forge::db::object::member<&account::usage>>>,
                                  forge::db::object::ordered_unique<by_name, forge::db::object::member<&account::name>>,
                                  forge::db::object::ordered_unique<by_key, forge::db::object::member<&account::key>>>>;

FORGE_DB_OBJECT(account_object)

template <typename View>
concept ranked_findable_by_name = requires(View& index, const std::string& name) {
   index.find_rank(name);
};

int main() {
   static_assert(forge::db::object::object_model<account_object>);
   static_assert(forge::db::object::system_object_value<forge::db::object::header>);
   static_assert(forge::db::object::system_object_model<forge::db::object::header_index>);
   static_assert(!forge::db::object::application_object_value<forge::db::object::header>);
   static_assert(forge::db::object::sortable_key<external_key>);
   static_assert(std::same_as<forge::db::object::index_for_id_t<account::id_t>, account_object>);
   static_assert(std::same_as<forge::db::object::index_for_id_t<forge::db::object::header::id_t>,
                              forge::db::object::header_index>);
   static_assert(requires(forge::db::object::store& store,
                          const forge::db::core::snapshot& view) {
      { store.join(view) } -> std::same_as<forge::db::object::snapshot>;
   });
   using ranked_view = forge::db::object::index_view<account_object, by_id>;
   using ordered_view = forge::db::object::index_view<account_object, by_name>;
   static_assert(requires(ranked_view& index, const account& value, account::id_t id) {
      index.count();
      index.sum<by_usage>();
      index.nth(0U);
      index.rank(value);
      index.find_rank(id);
      index.lower_bound_rank(id);
      index.upper_bound_rank(id);
      index.equal_range_rank(id);
   });
   static_assert(!ranked_findable_by_name<ordered_view>);
   constexpr auto type = forge::db::object::object_id_of<account_object>::value;
   const auto key = forge::db::core::record_key{std::vector<std::byte>{std::byte{0x01}}};
   return type.space == 1 && type.type == 7 && !key.empty() ? 0 : 1;
}
