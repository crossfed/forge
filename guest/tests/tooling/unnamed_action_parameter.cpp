#include <cstdint>

import forge.contract;

class [[forge::contract("unnamedaction")]] unnamedaction : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void transfer(std::uint64_t) {}
};
