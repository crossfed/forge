import forge.contract;
import product.chain.protocol;

class [[forge::contract("consumer")]] consumer_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void beginrev(product::chain::begin_revision request) {
      forge::contract::check(product::chain::checked_add(request.size, 0).has_value(), "revision size overflow");
      forge::contract::check(
          product::chain::implementation_file() != nullptr,
          "installed contract library source path is unavailable");
   }
};
