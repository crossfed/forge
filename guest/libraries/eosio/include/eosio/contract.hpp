#pragma once

import forge.contract;

namespace eosio {

class contract : public forge::contract::context {
 public:
   using context::context;
};

} // namespace eosio

#define CONTRACT class [[eosio::contract]]
#define ACTION [[eosio::action]] void
#define TABLE struct [[eosio::table]]
