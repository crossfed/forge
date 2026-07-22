#define FORGE_CONTRACT_MULTI_INDEX_SUITE 1
#include "multi_index_cases.hpp"

import forge.contract;

class [[forge::contract("midxerrors")]] multi_index_errors_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] std::uint32_t run(std::uint32_t scenario) {
      multi_index_cases::run(get_self(), scenario);
      return scenario;
   }
};
