import forge.contract;

class [[forge::contract("configuration")]] configuration_contract final
   : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void verify() {}
};
