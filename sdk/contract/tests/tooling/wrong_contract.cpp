import forge.contract;

class [[forge::contract("different")]] different : public forge::contract::base {
 public:
   using base::base;

   [[forge::action]] void run() {}
};
