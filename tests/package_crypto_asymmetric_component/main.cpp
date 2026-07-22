import forge.crypto.asymmetric;

int main() {
   const auto key = forge::crypto::asymmetric::private_key::generate_p256();
   return key.type() == forge::crypto::asymmetric::algorithm::p256 ? 0 : 1;
}
