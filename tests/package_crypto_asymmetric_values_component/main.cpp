#include <type_traits>

import forge.crypto.asymmetric.values;

int main() {
   using forge::crypto::asymmetric::algorithm;
   using forge::crypto::asymmetric::public_key;
   static_assert(std::is_default_constructible_v<public_key>);
   return static_cast<int>(algorithm::secp256k1);
}
