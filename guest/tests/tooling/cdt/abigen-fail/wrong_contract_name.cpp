import forge.contract;

#include "../cdt_support.hpp"

using namespace eosio;

class [[eosio::contract("hello")]] hello : public contract {
 public:
   using contract::contract;

   [[eosio::action]] void hi(name value) {
      print_f("Name : %\n", value);
   }
};
