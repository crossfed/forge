import forge.contract;

class [[forge::contract("different")]] different : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void run() {}
};
