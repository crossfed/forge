#include <string>

import forge.crypto.signer.types;

int main() {
   const auto id = forge::crypto::signer::key_id{.value = "package-key"};
   return id.value == "package-key" ? 0 : 1;
}
