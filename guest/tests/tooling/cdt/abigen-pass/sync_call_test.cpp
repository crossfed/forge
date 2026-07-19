#include <cstdint>

import forge.contract;

#include "../cdt_support.hpp"

class [[eosio::contract]] sync_call_test : public eosio::contract {
 public:
   using eosio::contract::contract;

   [[eosio::call]] std::uint32_t noparam() {
      return 10;
   }

   [[eosio::call]] std::uint32_t withparam(std::uint32_t in) {
      return in;
   }

   [[eosio::call]] void voidfunc() {
      int value = 10;
      static_cast<void>(value);
   }

   [[eosio::call]] std::uint32_t sum(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
      return a + b + c;
   }
};
