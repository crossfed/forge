#ifndef NDEBUG
#error "host Release must run Abigen with Release flags"
#endif

import forge.contract;
import product.contract.configuration;

class [[forge::contract("configuration")]] configuration_contract final : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void verify(product::contract::configuration::request value) {
      product::contract::configuration::verify(*this, value);
   }
};
