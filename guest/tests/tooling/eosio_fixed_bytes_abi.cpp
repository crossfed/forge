#include <eosio/eosio.hpp>
#include <eosio/fixed_bytes.hpp>

class [[eosio::contract("fixedbytes")]] fixedbytes : public eosio::contract {
 public:
   using contract::contract;

   [[eosio::action]] void verify(eosio::checksum160 one, eosio::checksum256 two, eosio::checksum512 three) {
      static_cast<void>(one);
      static_cast<void>(two);
      static_cast<void>(three);
   }
};
