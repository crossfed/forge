#include <cstdint>

import forge.contract;

class [[forge::contract("static_apply")]] static_apply : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void run() {}
};

static void apply() {}
