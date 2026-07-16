import forge.contract;

class [[forge::contract("invalid_exported_apply")]] invalid_exported_apply : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void run() {}
};

extern "C" [[gnu::visibility("default")]] void apply() {}
