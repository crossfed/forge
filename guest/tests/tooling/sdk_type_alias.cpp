#include <eosio/eosio.hpp>
#include <eosio/producer_schedule.hpp>

class [[eosio::contract("sdkalias")]] sdkalias : public eosio::contract {
 public:
   using contract::contract;

   [[eosio::action]] void setprods(eosio::producer_authority authority) {
      static_cast<void>(authority);
   }
};
