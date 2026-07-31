import forge.contract;
import forge.tests.aligned_multi_index;

class [[forge::contract("alignedidx")]] aligned_multi_index_contract final
   : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void create() {
      forge::tests::aligned_multi_index::create(*this);
   }
};
