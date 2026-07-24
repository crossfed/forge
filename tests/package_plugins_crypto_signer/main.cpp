#include <concepts>
#include <string_view>

import forge.crypto.asymmetric;
import forge.crypto.bls;
import forge.plugins.crypto.signer.bls_api;
import forge.plugins.crypto.signer.plugin;
import forge.plugins.crypto.signer.types;

int main() {
   using response = forge::plugins::crypto::signer::response;
   static_assert(std::same_as<decltype(response::public_key), forge::crypto::asymmetric::public_key>);
   static_assert(std::same_as<decltype(response::signature), forge::crypto::asymmetric::signature>);
   using bls_description = forge::plugins::crypto::signer::bls_description;
   using bls_response = forge::plugins::crypto::signer::bls_response;
   static_assert(std::same_as<decltype(bls_description::public_key), forge::crypto::bls::public_key>);
   static_assert(std::same_as<decltype(bls_description::proof_of_possession), forge::crypto::bls::signature>);
   static_assert(std::same_as<decltype(bls_response::signature), forge::crypto::bls::signature>);

   const auto descriptor = forge::plugins::crypto::signer::descriptor();
   const auto bls = forge::plugins::crypto::signer::bls_api::ref();
   return descriptor.id.value == "forge.plugins.crypto.signer" &&
                  bls.id.value == std::string_view{"forge.plugins.crypto.signer.bls"}
              ? 0
              : 1;
}
