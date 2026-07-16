#include <cstdint>
#include <string>
#include <vector>

import forge.contract;

class [[forge::contract("parity")]] parity : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void greet(std::string user, std::vector<std::uint32_t> values) {}
};
