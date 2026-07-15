import forge.contract;

#include "../cdt_support.hpp"

using namespace eosio;

class [[eosio::contract]] ricardian_contract_test : public contract {
 public:
   using contract::contract;

   [[eosio::action]] void test() {}
};
