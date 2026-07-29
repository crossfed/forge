#include <cstdint>

import forge.contract;

struct begin_revision {
   std::uint64_t workspace = 0;

   static constexpr forge::chain::protocol::action_name get_name() {
      return forge::chain::protocol::make_name("beginrev");
   }
};

class [[forge::contract("namedmismatch")]] named_action_mismatch_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action("different")]] void submit(begin_revision request) {
      static_cast<void>(request);
   }
};
