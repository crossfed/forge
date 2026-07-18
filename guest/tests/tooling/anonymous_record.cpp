#include <cstdint>

import forge.contract;

namespace {

struct payload {
   std::uint32_t value = 0;
};

} // namespace

class [[forge::contract("anonymous")]] anonymous : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void store(payload value) {
      static_cast<void>(value);
   }
};
