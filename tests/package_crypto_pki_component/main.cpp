import forge.crypto.pki.x509;

int main() {
   const auto certificate = forge::crypto::pki::x509::certificate{};
   return certificate.der().empty() ? 0 : 1;
}
