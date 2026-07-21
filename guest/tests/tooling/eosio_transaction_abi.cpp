#include <eosio/eosio.hpp>
#include <eosio/transaction.hpp>

class [[eosio::contract("transactionabi")]] transactionabi : public eosio::contract {
 public:
   using contract::contract;

   [[eosio::action("sendtrx")]] void submit(eosio::transaction value) {
      static_cast<void>(value);
   }
};
