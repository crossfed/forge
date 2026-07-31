import forge.contract;
import product.chain.protocol;
import product.contract.revision;

namespace product::contract {

class [[forge::contract("product")]] entry final
   : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void submit(chain::begin_revision request) {
      revision::submit(*this, request);
   }
};

} // namespace product::contract
