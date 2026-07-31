#include <cstdint>

import forge.contract;
import forge.contract.multi_index;

using forge::chain::protocol::literals::operator""_n;

struct create_payload {
   std::uint64_t id = 0;

   static constexpr forge::chain::protocol::action_name get_name() {
      return forge::chain::protocol::make_name("create");
   }
};

struct [[forge::table("records.")]] record {
   std::uint64_t id = 0;

   static constexpr forge::chain::protocol::table_name get_table_name() {
      return forge::chain::protocol::make_name("records");
   }

   std::uint64_t primary_key() const {
      return id;
   }
};

using records = forge::contract::multi_index<"records"_n, record>;

class [[forge::contract("equivalent")]] equivalent_contract final
   : public forge::contract::context {
 public:
   using context::context;

   [[forge::action("create.")]] void create(create_payload payload) {
      records rows{get_self(), get_self().value};
      rows.emplace(get_self(), [&](auto& row) { row.id = payload.id; });
   }
};
