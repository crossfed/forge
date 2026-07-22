import forge.crypto.bls.values;

int main() {
   const auto signature = forge::crypto::bls::signature_value{};
   return signature.size() == 192 ? 0 : 1;
}
