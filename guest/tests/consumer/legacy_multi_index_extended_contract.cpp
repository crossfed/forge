#define FORGE_CONTRACT_TEST_LEGACY_MULTI_INDEX 1
#define FORGE_CONTRACT_MULTI_INDEX_SUITE 2
#include "multi_index_cases.hpp"

class [[eosio::contract("legmidxext")]] legacy_multi_index_extended_contract : public eosio::contract {
 public:
   using contract::contract;

   [[eosio::action]] std::uint32_t run(std::uint32_t scenario) {
      multi_index_cases::run(get_self(), scenario);
      return scenario;
   }
};

EOSIO_DISPATCH(legacy_multi_index_extended_contract, (run))
