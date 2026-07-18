#include <cstdint>

import forge.contract;

namespace product {

template <typename T> struct vector {
   T value{};
};

} // namespace product

class [[forge::contract("uservector")]] uservector : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void store(product::vector<std::uint32_t> value) {
      static_cast<void>(value);
   }
};
