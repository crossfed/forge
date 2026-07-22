#include <string>

import forge.crypto.digest.sha256;

int main() {
   const auto value = forge::crypto::digest::sha256::hash(std::string{"forge"});
   return value.data_size() == 32 ? 0 : 1;
}
