import forge.contract;

class [[forge::contract("minimal")]] minimal : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void ping(unsigned long long) {}
};
