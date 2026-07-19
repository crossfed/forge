import forge.contract;

class [[forge::contract("surface")]] surface : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void ping() {}
};
