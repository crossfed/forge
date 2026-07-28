#include <string>

import forge.crypto.digest.sha256;

int main() {
   const auto bytes{forge::crypto::digest::sha256::hash(std::string{"forge"}).to_uint8_span()};
   return bytes.front();
}
