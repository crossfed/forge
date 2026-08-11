import forge.crypto.bls.values;

int main() {
   const auto key = forge::crypto::bls::public_key{};
   const auto signature = forge::crypto::bls::signature{};
   const auto aggregate = forge::crypto::bls::aggregate_signature{};
   return key.bytes().size() == 96U && signature.bytes().size() == 192U && aggregate.bytes().size() == 192U ? 0 : 1;
}
