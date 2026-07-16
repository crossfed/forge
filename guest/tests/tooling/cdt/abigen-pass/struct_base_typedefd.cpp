import forge.contract;

#include "../cdt_support.hpp"

using namespace eosio;

struct foo {
   int value;
};

using bar = foo;

struct baz : bar {};

class [[eosio::contract]] struct_base_typedefd : public contract {
 public:
   using contract::contract;

   [[eosio::action]] void hi(baz b) {
      print(b.value);
   }
};
