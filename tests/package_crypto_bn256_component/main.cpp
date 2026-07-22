import forge.crypto.bn256;

int main() {
   const auto pairing = &forge::crypto::bn256::pairing_check;
   return pairing == nullptr ? 1 : 0;
}
