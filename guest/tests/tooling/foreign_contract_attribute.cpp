import forge.contract;

class [[product::contract("foreignscope")]] foreign_contract : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void run() {}
};
