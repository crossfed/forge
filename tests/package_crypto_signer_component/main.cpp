#include <string>

import forge.crypto.asymmetric;
import forge.crypto.core.secret_string;
import forge.crypto.signer.configured_provider;

int main() {
   const auto key = forge::crypto::asymmetric::private_key::generate();
   const auto encoded = forge::crypto::asymmetric::encoding::forge().format(key);
   const auto value = forge::crypto::signer::configured_provider::create({
       .id = {.value = "package-key"},
       .private_key = forge::crypto::core::secret_string{encoded},
   });
   return value ? 0 : 1;
}
