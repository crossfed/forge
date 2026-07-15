#include <cstdint>
#include <string>
#include <vector>

import forge.contract;

class [[forge::contract("hello")]] hello : public forge::contract::base {
 public:
   using base::base;

   [[forge::action]] void greet(std::string user, std::vector<std::uint32_t> values) {
      forge::contract::check(!user.empty(), "user must not be empty");
      forge::contract::check(!values.empty(), "values must not be empty");
   }

   [[forge::action]] void values(forge::chain::protocol::name account, forge::chain::protocol::symbol symbol,
                                 forge::chain::protocol::asset quantity) {
      forge::contract::check(account.value == 6138663577826885632ULL, "unexpected name encoding");
      forge::contract::check(symbol.raw() == 1398362884ULL, "unexpected symbol encoding");
      forge::contract::check(quantity.amount == 42 && quantity.sym == symbol, "unexpected asset encoding");
   }
};
