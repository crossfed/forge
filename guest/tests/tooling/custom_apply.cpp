#include <cstdint>

import forge.contract;
import forge.raw.codec;

struct custom_record {
   std::uint64_t value = 0;
};

class [[forge::contract("custom_apply")]] custom_apply : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void run(custom_record value) {
      const auto packed = forge::raw::pack(value);
      forge::contract::check(!packed.empty(), "custom record was not packed");
   }
};

extern "C" [[gnu::visibility("default")]] void apply(std::uint64_t, std::uint64_t, std::uint64_t) {}
