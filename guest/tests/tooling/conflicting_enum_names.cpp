module;

#include <cstdint>

import forge.contract;

namespace application {

enum class status : std::uint8_t {
   active,
};

}

namespace workspace {

enum class status : std::uint8_t {
   active,
};

}

class [[forge::contract("enumconflict")]] enum_conflict_contract final
   : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void compare(application::status application_status,
                                  workspace::status workspace_status) {}
};
