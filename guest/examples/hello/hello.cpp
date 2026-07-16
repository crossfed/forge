#include <cstdint>
#include <string>
#include <vector>

import forge.contract;

class [[forge::contract("hello")]] hello : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void greet(std::string user, std::vector<std::uint32_t> values) {
      forge::contract::check(!user.empty(), "user must not be empty");
      forge::contract::check(!values.empty(), "values must not be empty");
   }
};
