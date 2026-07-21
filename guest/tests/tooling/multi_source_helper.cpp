#include "multi_source_contract.hpp"

std::uint32_t increment(std::uint32_t value) {
   return value + 1U;
}

class [[forge::contract("multisource")]] multisource_helper : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] std::uint32_t previous(std::uint32_t value) {
      return value - 1U;
   }
};
