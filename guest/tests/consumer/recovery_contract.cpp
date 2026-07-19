#include <forge/contract/intrinsics.h>

import forge.contract;
import forge.contract.crypto;

class [[forge::contract("recovery")]] recovery : public forge::contract::context {
 public:
   using context::context;

   [[forge::action]] bool recoverkey(forge::contract::checksum256 digest, forge::contract::signature value,
                                     forge::contract::public_key expected) const {
      const auto actual = forge::contract::recover_key(digest, value);
      forge::contract::assert_recover_key(digest, value, actual);
      forge::contract::assert_recover_key(digest, value, expected);
      return true;
   }
};
