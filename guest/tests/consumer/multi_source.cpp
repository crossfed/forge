#include <cstdint>

import forge.contract;

std::uint32_t increment(std::uint32_t value);

class [[forge::contract("multisource")]] multisource : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] std::uint32_t next(std::uint32_t value) {
      return increment(value);
   }
};
