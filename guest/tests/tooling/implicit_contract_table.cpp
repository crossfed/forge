#include <cstdint>

import forge.contract;

template <std::uint64_t Name, typename Value> struct multi_index {
   Value value{};
};

class [[forge::contract]] implicit_contract : public forge::contract::context {
 public:
   using context::context;

   struct record {
      std::uint64_t id = 0;
   };

   [[forge::action]] void run() {}
};

using records = multi_index<8417982951132397568ULL, implicit_contract::record>;
