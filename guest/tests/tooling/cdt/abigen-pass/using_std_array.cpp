#include <array>
#include <cstdint>

import forge.contract;

#include "../cdt_support.hpp"

using namespace eosio;
using std::array;

class [[eosio::contract("using_std_array")]] using_std_array : public contract {
 public:
   using contract::contract;

   [[eosio::action]] void hi(name user) {
      require_auth(user);
      print("Hello, ", user);
   }

   struct [[eosio::table]] greeting {
      std::uint64_t id;
      array<int, 32> t;

      std::uint64_t primary_key() const {
         return id;
      }
   };

   using greeting_index = multi_index<"greeting"_n, greeting>;
};
