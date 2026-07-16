#include <cstdint>

import forge.contract;

class [[forge::contract("custom_apply")]] custom_apply : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void run() {}
};

extern "C" [[gnu::visibility("default")]] void apply(std::uint64_t, std::uint64_t, std::uint64_t) {}
