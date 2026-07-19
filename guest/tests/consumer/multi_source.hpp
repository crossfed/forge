#pragma once

#include <cstdint>

import forge.contract;
import forge.raw.codec;

namespace multi_source_types {

struct value {
   std::uint32_t number = 0;
};

} // namespace multi_source_types

multi_source_types::value increment(multi_source_types::value value);

class [[forge::contract("multisource")]] multisource : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] multi_source_types::value next(multi_source_types::value value) {
      return increment(value);
   }
};
