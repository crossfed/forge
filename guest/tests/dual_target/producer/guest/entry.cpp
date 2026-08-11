import forge.contract;
import forge.chain.savanna.values;
import forge.raw.codec;
import product.chain.protocol;
import product.contract.revision;

namespace product::contract {

class [[forge::contract("product")]] entry final : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void submit(chain::begin_revision request) {
      revision::submit(*this, request);
   }

   [[forge::action]] void blswire(forge::chain::savanna::finalizer value) {
      const auto wire = forge::raw::pack(value);
      forge::contract::check(wire.size() == 107U, "unexpected BLS finalizer wire size");
      forge::contract::check(wire[0] == 1U && wire[1] == static_cast<unsigned char>('f'),
                             "unexpected BLS description wire");
      forge::contract::check(wire[2] == 7U && wire[10] == 96U, "unexpected BLS finalizer scalar wire");
      for (auto index = 11U; index < wire.size(); ++index) {
         forge::contract::check(wire[index] == 0U, "unexpected BLS public key wire");
      }
   }
};

} // namespace product::contract
