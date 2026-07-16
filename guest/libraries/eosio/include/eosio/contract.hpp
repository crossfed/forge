#pragma once

import forge.contract;

namespace eosio {

class contract : public forge::contract::base {
 public:
   using base::base;
};

} // namespace eosio

#define CONTRACT class [[eosio::contract]]
#define ACTION [[eosio::action]] void
#define TABLE struct [[eosio::table]]
