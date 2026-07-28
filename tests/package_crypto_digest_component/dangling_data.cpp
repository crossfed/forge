#include <string>

import forge.crypto.digest.sha256;

const char* dangling() {
   return forge::crypto::digest::sha256::hash(std::string{"forge"}).data();
}

int main() {
   return *dangling();
}
