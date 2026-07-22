import forge.contract;

class [[forge::contract("duplicate")]] duplicate : public forge::contract::context {
 public:
   using context::context;

   [[forge::action("same")]] void first() {}
   [[forge::action("same")]] void second() {}
};
