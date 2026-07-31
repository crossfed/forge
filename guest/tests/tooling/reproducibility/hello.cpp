import forge.contract;
import reproducibility.path;

class [[forge::contract("hello")]] hello final
   : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] void verify() {
      forge::contract::check(
         reproducibility::source_path()[0] != '\0',
         "source path must not be empty"
      );
   }
};
