#include <cstdint>
#include <string>
#include <vector>

import forge.contract;
import forge.raw.codec;

namespace contract_types {

struct record_value {
   std::string user;
   std::vector<std::uint32_t> values;
};

} // namespace contract_types

class [[forge::contract("recordtest")]] record_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] contract_types::record_value echo(contract_types::record_value value) {
      const auto packed = forge::raw::pack(value);
      return forge::raw::unpack_exact<contract_types::record_value>(packed);
   }
};
