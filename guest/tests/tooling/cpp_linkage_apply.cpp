#include <cstdint>

import forge.contract;

class [[forge::contract("cpp_linkage_apply")]] cpp_linkage_apply : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void run() {}
};

void apply(std::uint64_t, std::uint64_t, std::uint64_t) {}
