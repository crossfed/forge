#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/describe.hpp>
#include <forge/db/object/macros.hpp>

import forge.ids.object_id;
import forge.db.core.record;
import forge.db.object.index;
import forge.db.object.object;
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
};

BOOST_DESCRIBE_STRUCT(account, (forge::db::object::object<account, 1, 7>), (name, key))

struct by_id;
struct by_name;
struct by_key;

using account_object = forge::db::object::object_index<
    account,
    forge::db::object::indexed_by<forge::db::object::primary_unique<by_id>,
                                  forge::db::object::ordered_unique<by_name, forge::db::object::member<&account::name>>,
                                  forge::db::object::ordered_unique<by_key, forge::db::object::member<&account::key>>>>;

FORGE_DB_OBJECT(account_object)

int main() {
   static_assert(forge::db::object::object_model<account_object>);
   static_assert(forge::db::object::sortable_key<external_key>);
   static_assert(std::same_as<forge::ids::type_for_id_t<account::id_t>, account_object>);
   static_assert(std::same_as<forge::db::object::object_index_for_id_t<account::id_t>, account_object>);
   constexpr auto type = forge::db::object::object_id_of<account_object>::value;
   const auto key = forge::db::core::record_key{std::vector<std::byte>{std::byte{0x01}}};
   return type.space == 1 && type.type == 7 && !key.empty() ? 0 : 1;
}
