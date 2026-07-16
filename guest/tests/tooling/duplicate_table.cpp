#include <cstdint>

import forge.contract;

class [[forge::contract("duplicate")]] duplicate : public forge::contract::context {
 public:
   using context::context;

   struct [[forge::table("accounts")]] first {
      std::uint32_t value = 0;
   };

   struct [[forge::table("accounts")]] second {
      std::uint64_t value = 0;
   };
};
