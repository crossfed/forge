#if !defined(FORGE_CONTRACT_GUEST)
#error "contract analysis and compilation must use the guest translation unit"
#endif

#include <cstdint>

import forge.contract;

class [[forge::contract("guestmacro")]] guestmacro : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void run(std::uint64_t value) {
      forge::contract::check(value != 0, "value must not be zero");
   }
};
