#include <cstdint>

import forge.contract;
import forge.contract.multi_index;

using forge::chain::protocol::literals::operator""_n;

struct [[forge::table("wrong")]] attributed_table_record {
   std::uint64_t id = 0;

   static constexpr forge::chain::protocol::table_name get_table_name() {
      return forge::chain::protocol::make_name("actual");
   }

   std::uint64_t primary_key() const {
      return id;
   }
};

using attributed_table =
   forge::contract::multi_index<"actual"_n, attributed_table_record>;

class [[forge::contract("attrtable")]] attributed_table_contract final
   : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void create() {
      attributed_table rows{get_self(), get_self().value};
      rows.emplace(get_self(), [](auto& row) { row.id = 1; });
   }
};
