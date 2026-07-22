#include <forge/contract/serialize.hpp>

import forge.chain.protocol.values;
import forge.contract;
import forge.contract.multi_index;

using forge::chain::protocol::literals::operator""_n;

struct [[forge::table("records")]] record {
   std::uint64_t id = 0;
   std::uint64_t value = 0;

   [[nodiscard]] std::uint64_t primary_key() const {
      return id;
   }

   [[nodiscard]] std::uint64_t by_value() const {
      return value;
   }

   FORGE_SERIALIZE(record, &record::id, &record::value)
};

using first =
    forge::contract::indexed_by<"byvalue"_n, forge::contract::const_mem_fun<record, std::uint64_t, &record::by_value>>;
using second =
    forge::contract::indexed_by<"byvalue"_n, forge::contract::const_mem_fun<record, std::uint64_t, &record::by_value>>;
using records = forge::contract::multi_index<"records"_n, record, first, second>;

class [[forge::contract("duplicateindex")]] duplicate_index_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void run() {
      auto table = records{get_self(), get_self().value};
      (void)table;
   }
};
