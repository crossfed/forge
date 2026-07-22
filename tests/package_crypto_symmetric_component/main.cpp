import forge.crypto.symmetric.aes;

int main() {
   const auto key = forge::crypto::symmetric::aes::aes256_key{};
   return key.bytes.size() == forge::crypto::symmetric::aes::aes256_key_size ? 0 : 1;
}
