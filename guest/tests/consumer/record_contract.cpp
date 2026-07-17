#include <cstdint>
#include <forge/contract/serialize.hpp>
#include <string>
#include <vector>

import forge.contract;

struct record_value {
   std::string user;
   std::vector<std::uint32_t> values;

   FORGE_SERIALIZE(record_value, &record_value::user, &record_value::values)
};

class [[forge::contract("recordtest")]] record_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] record_value echo(record_value value) {
      return value;
   }
};
