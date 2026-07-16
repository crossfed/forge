#include <cstdint>

import forge.contract;

class [[forge::contract("overloaded")]] overloaded : public forge::contract::context {
 public:
   using context::context;

   [[forge::action("first")]] void run(std::uint32_t value) {
      static_cast<void>(value);
   }

   [[forge::action("second")]] void run(std::uint64_t value) {
      static_cast<void>(value);
   }
};
