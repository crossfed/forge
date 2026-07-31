module;

#include <cstdint>

import forge.contract;
import forge.contract.multi_index;

using forge::chain::protocol::literals::operator""_n;

struct invalid_table_record {
   std::uint64_t id = 0;

   std::uint64_t get_table_name() const {
      return 0;
   }

   std::uint64_t primary_key() const {
      return id;
   }
};

using invalid_table =
   forge::contract::multi_index<"invalidname"_n, invalid_table_record>;

class [[forge::contract("invalidtable")]] invalid_table_contract final
   : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void create() {
      invalid_table rows{get_self(), get_self().value};
      rows.emplace(get_self(), [](auto& row) { row.id = 1; });
   }
};
