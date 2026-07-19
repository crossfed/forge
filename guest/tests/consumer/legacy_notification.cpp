#include <cstdint>

#include <eosio/eosio.hpp>

class [[eosio::contract("legacynotify")]] legacynotify : public eosio::contract {
 public:
   using contract::contract;

   [[eosio::on_notify("eosio.token::transfer")]] std::uint32_t transfer(std::uint32_t value) {
      return value + 1U;
   }

   [[eosio::on_notify("*::fallback")]] std::uint32_t fallback(std::uint32_t value) {
      return value + 2U;
   }

   [[eosio::action]] bool actionmode() const {
      return is_sync_call();
   }

   [[eosio::call]] bool callmode() const {
      return is_sync_call();
   }
};
