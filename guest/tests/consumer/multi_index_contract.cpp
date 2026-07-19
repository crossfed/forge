#define FORGE_CONTRACT_MULTI_INDEX_SUITE 0
#include "multi_index_cases.hpp"

import forge.contract;

class [[forge::contract("multiidx")]] multi_index_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] std::uint32_t run(std::uint32_t scenario) {
      multi_index_cases::run(get_self(), scenario);
      return scenario;
   }
};
