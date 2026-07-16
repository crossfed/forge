import forge.contract;

class [[forge::contract("foreignscope")]] foreign_action : public forge::contract::context {
 public:
   using context::context;

   [[product::action]] void run() {}
};
