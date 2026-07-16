#include <cstdint>
#include <string>
#include <vector>

import forge.contract;

#include "../cdt_support.hpp"

using namespace eosio;

struct test_res {
   int a;
   float b;
   name c;
};

class [[eosio::contract]] action_results_test : public contract {
 public:
   using contract::contract;

   [[eosio::action]] void action1() {}

   [[eosio::action]] std::uint32_t action2() {
      return 42;
   }

   [[eosio::action]] std::string action3() {
      return "foo";
   }

   [[eosio::action]] std::vector<name> action4() {
      return {"dan"_n};
   }

   [[eosio::action]] test_res action5() {
      return {4, 42.4F, "bucky"_n};
   }
};
