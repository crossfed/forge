#include <cstdint>

import forge.contract;

#include "../cdt_support.hpp"
#include "singleton_contract_support.hpp"

using namespace eosio;

struct [[eosio::table]] out_of_class2 {
   std::uint64_t id;

   std::uint64_t primary_key() const {
      return id;
   }
};

using out_of_class_index51 = eosio::multi_index<"mi.config5"_n, out_of_class2>;
using uout_of_class_index51 = eosio::multi_index<"mi.config51"_n, out_of_class2>;

struct [[eosio::table, eosio::contract("singleton_contract")]] out_of_class3 {
   std::uint64_t id;

   std::uint64_t primary_key() const {
      return id;
   }
};

using out_of_class_index52 = eosio::multi_index<"mi.config52"_n, out_of_class3>;
using smpl_config5 = eosio::singleton<"smpl.conf5"_n, eosio::name>;
using config5 = eosio::singleton<"config5"_n, out_of_class2>;
using smpl_config51 = smpl_config5;
using config51 = config5;
using smpl_conf51 = eosio::singleton<"smpl.conf51"_n, eosio::name>;
using config52 = eosio::singleton<"config52"_n, out_of_class2>;
using smpl_conf52 = smpl_conf51;
using config53 = config51;

class [[eosio::contract("singleton_contract")]] singleton_contract : public contract {
 public:
   using contract::contract;

   [[eosio::action]] void whatever() {}

   struct [[eosio::table]] tbl_config {
      std::uint64_t y;
      std::uint64_t x;
   };

   using config = eosio::singleton<"config"_n, tbl_config>;
   using smpl_config = eosio::singleton<"smpl.config"_n, name>;
   using smpl_config2 = smpl_config5;
   using config2 = config551;
};
