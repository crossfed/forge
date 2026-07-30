#include "support/local_value.hpp"
#include <product/chain/version.hpp>

import forge.contract;
import product.chain.protocol;
import product.contract.revision;

static_assert(product::chain::protocol_version == 1);
static_assert(product_contract_local_value == 42);

class [[forge::contract("product")]] product_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void submit(product::chain::begin_revision request) {
      product::contract::revision::submit(*this, request);
   }
};
