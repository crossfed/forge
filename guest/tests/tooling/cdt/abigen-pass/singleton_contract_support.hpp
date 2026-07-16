#pragma once

struct [[eosio::table]] out_of_class {
   std::uint64_t id;

   std::uint64_t primary_key() const {
      return id;
   }
};

using out_of_class_index = eosio::multi_index<"mi.config55"_n, out_of_class>;
using uout_of_class_index = eosio::multi_index<"mi.config551"_n, out_of_class>;

using smpl_config55 = eosio::singleton<"smpl.conf55"_n, eosio::name>;
using config55 = eosio::singleton<"config55"_n, out_of_class>;
using smpl_config551 = smpl_config55;
using config551 = config55;
using smpl_conf551 = eosio::singleton<"smpl.conf551"_n, eosio::name>;
using config552 = eosio::singleton<"config552"_n, out_of_class>;
using smpl_conf552 = smpl_conf551;
using config553 = config551;

class [[eosio::contract("singleton_contract_simple2")]] singleton_contract_simple2 : public eosio::contract {
 public:
   using eosio::contract::contract;

   [[eosio::action]] void whatever() {}

   struct [[eosio::table]] inside_class {
      std::uint64_t id;

      std::uint64_t primary_key() const {
         return id;
      }
   };

   using smpl_conf552 = eosio::singleton<"smpl.conf552"_n, eosio::name>;
   using config552 = eosio::singleton<"config552"_n, inside_class>;
   using smpl_conf553 = smpl_conf552;
   using config553 = config552;
   using smpl_conf554 = eosio::singleton<"smpl.conf554"_n, eosio::name>;
   using config554 = eosio::singleton<"config554"_n, inside_class>;
   using smpl_conf555 = smpl_conf554;
   using config555 = config554;
   using inside_class_index = eosio::multi_index<"mi.config553"_n, inside_class>;
   using uinside_class_index = eosio::multi_index<"mi.config554"_n, inside_class>;
};
