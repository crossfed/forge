import forge.crypto.digest.sha256;

int main() {
   auto encoder = forge::crypto::digest::sha256::encoder{};
   const auto bytes = encoder.result().to_uint8_span();
   return bytes.front();
}
