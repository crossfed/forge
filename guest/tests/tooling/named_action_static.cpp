#include <cstdint>

import forge.contract;

struct begin_revision {
   std::uint64_t workspace = 0;

   static constexpr forge::chain::protocol::action_name get_name() {
      return forge::chain::protocol::make_name("beginrev");
   }
};

class [[forge::contract("namedstatic")]] named_action_static_contract : public forge::contract::context {
 public:
   using context::context;

   [[clang::annotate("forge.action")]] static void submit(begin_revision request) {
      static_cast<void>(request);
   }
};
