#include <concepts>

import forge.crypto.asymmetric;
import forge.plugins.crypto.signer.plugin;
import forge.plugins.crypto.signer.types;

int main() {
   using response = forge::plugins::crypto::signer::response;
   static_assert(std::same_as<decltype(response::public_key), forge::crypto::asymmetric::public_key>);
   static_assert(std::same_as<decltype(response::signature), forge::crypto::asymmetric::signature>);

   const auto descriptor = forge::plugins::crypto::signer::descriptor();
   return descriptor.id.value == "forge.plugins.crypto.signer" ? 0 : 1;
}
