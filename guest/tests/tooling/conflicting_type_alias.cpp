#include <cstdint>
#include <string>

import forge.contract;

namespace first {

using id = std::uint64_t;

} // namespace first

namespace second {

using id = std::string;

} // namespace second

class [[forge::contract("aliasclash")]] aliasclash : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void collide(first::id first_id, second::id second_id) {
      static_cast<void>(first_id);
      static_cast<void>(second_id);
   }
};
