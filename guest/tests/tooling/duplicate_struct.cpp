#include <cstdint>

import forge.contract;

namespace first {

struct payload {
   std::uint32_t value = 0;
};

} // namespace first

namespace second {

struct payload {
   std::uint64_t value = 0;
   std::uint64_t extra = 0;
};

} // namespace second

class [[forge::contract("duplicate")]] duplicate : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void one(first::payload value) {
      static_cast<void>(value);
   }

   [[forge::action]] void two(second::payload value) {
      static_cast<void>(value);
   }
};
