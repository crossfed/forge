#include <cstdint>

import forge.contract;

class [[forge::contract("unnamedcall")]] unnamedcall : public forge::contract::context {
 public:
   using context::context;

   [[forge::call]] std::uint64_t lookup(std::uint64_t) {
      return 0;
   }
};
