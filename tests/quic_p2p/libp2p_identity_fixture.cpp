#include "libp2p_identity_fixture.hxx"

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

import forge.crypto.asymmetric;
import forge.crypto.asymmetric.rsa;
import forge.crypto.pki.der;
import forge.crypto.pki.pem;
import forge.net.p2p.identity;

namespace forge::tests::p2p {
namespace {

using bio_ptr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using evp_pkey_ptr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using evp_pkey_ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using x509_ptr = std::unique_ptr<X509, decltype(&X509_free)>;
using asn1_object_ptr = std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)>;
using asn1_octet_string_ptr = std::unique_ptr<ASN1_OCTET_STRING, decltype(&ASN1_OCTET_STRING_free)>;
using x509_extension_ptr = std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)>;

[[nodiscard]] std::string bio_to_string(BIO* value) {
   BUF_MEM* buffer = nullptr;
   BIO_get_mem_ptr(value, &buffer);
   if (buffer == nullptr || buffer->data == nullptr) {
      throw std::runtime_error{"failed to read test identity BIO"};
   }
   return {buffer->data, buffer->length};
}

void append_der_length(std::vector<std::uint8_t>& out, std::size_t length) {
   if (length < 0x80U) {
      out.push_back(static_cast<std::uint8_t>(length));
      return;
   }
   auto bytes = std::vector<std::uint8_t>{};
   while (length != 0U) {
      bytes.push_back(static_cast<std::uint8_t>(length & 0xffU));
      length >>= 8U;
   }
   out.push_back(static_cast<std::uint8_t>(0x80U | bytes.size()));
   out.insert(out.end(), bytes.rbegin(), bytes.rend());
}

void append_der_octet_string(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> value) {
   out.push_back(0x04U);
   append_der_length(out, value.size());
   out.insert(out.end(), value.begin(), value.end());
}

[[nodiscard]] std::vector<std::uint8_t> signed_key_der(std::span<const std::uint8_t> public_key,
                                                       std::span<const std::uint8_t> signature) {
   auto content = std::vector<std::uint8_t>{};
   append_der_octet_string(content, public_key);
   append_der_octet_string(content, signature);
   auto out = std::vector<std::uint8_t>{0x30U};
   append_der_length(out, content.size());
   out.insert(out.end(), content.begin(), content.end());
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> certificate_public_key_der(X509* certificate) {
   auto key = evp_pkey_ptr{X509_get_pubkey(certificate), &EVP_PKEY_free};
   if (!key) {
      throw std::runtime_error{"failed to get test certificate public key"};
   }
   const auto length = i2d_PUBKEY(key.get(), nullptr);
   if (length <= 0) {
      throw std::runtime_error{"failed to size test certificate public key"};
   }
   auto out = std::vector<std::uint8_t>(static_cast<std::size_t>(length));
   auto* cursor = out.data();
   if (i2d_PUBKEY(key.get(), &cursor) != length) {
      throw std::runtime_error{"failed to encode test certificate public key"};
   }
   return out;
}

[[nodiscard]] std::vector<std::uint8_t> tls_identity_message(std::span<const std::uint8_t> public_key) {
   constexpr auto prefix = std::string_view{"libp2p-tls-handshake:"};
   auto out = std::vector<std::uint8_t>(prefix.begin(), prefix.end());
   out.insert(out.end(), public_key.begin(), public_key.end());
   return out;
}

template <typename Range> [[nodiscard]] std::vector<std::uint8_t> bytes_from_range(const Range& value) {
   auto out = std::vector<std::uint8_t>{};
   out.reserve(value.size());
   for (const auto byte : value) {
      out.push_back(static_cast<std::uint8_t>(byte));
   }
   return out;
}

} // namespace

identity_fixture make_identity_fixture(std::string_view common_name) {
   auto key_context = evp_pkey_ctx_ptr{EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), &EVP_PKEY_CTX_free};
   if (!key_context || EVP_PKEY_keygen_init(key_context.get()) != 1 ||
       EVP_PKEY_CTX_set_rsa_keygen_bits(key_context.get(), 2048) != 1) {
      throw std::runtime_error{"failed to initialize test identity key generation"};
   }

   EVP_PKEY* raw_key = nullptr;
   if (EVP_PKEY_keygen(key_context.get(), &raw_key) != 1 || raw_key == nullptr) {
      throw std::runtime_error{"failed to generate test identity key"};
   }
   auto key = evp_pkey_ptr{raw_key, &EVP_PKEY_free};
   auto private_key_bio = bio_ptr{BIO_new(BIO_s_mem()), &BIO_free};
   if (!private_key_bio ||
       PEM_write_bio_PrivateKey(private_key_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) {
      throw std::runtime_error{"failed to encode test identity private key"};
   }
   auto private_key_pem = bio_to_string(private_key_bio.get());

   auto certificate = x509_ptr{X509_new(), &X509_free};
   if (!certificate || X509_set_version(certificate.get(), 2) != 1 ||
       ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()),
                        static_cast<long>(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7fffffff)) !=
           1 ||
       X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60) == nullptr ||
       X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 24 * 60 * 60) == nullptr ||
       X509_set_pubkey(certificate.get(), key.get()) != 1) {
      throw std::runtime_error{"failed to configure test identity certificate"};
   }
   auto* name = X509_get_subject_name(certificate.get());
   if (name == nullptr ||
       X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(common_name.data()),
                                  static_cast<int>(common_name.size()), -1, 0) != 1 ||
       X509_set_issuer_name(certificate.get(), name) != 1) {
      throw std::runtime_error{"failed to configure test identity subject"};
   }

   const auto identity_private_key = forge::crypto::pki::pem::read_private_key(private_key_pem);
   const auto identity_key = forge::net::p2p::public_key{
       .type = forge::net::p2p::public_key::type::rsa,
       .data = forge::crypto::pki::der::write_public_key(identity_private_key.get_public_key()),
   };
   const auto message = tls_identity_message(certificate_public_key_der(certificate.get()));
   const auto signature = bytes_from_range(
       std::get<forge::crypto::asymmetric::rsa_signature>(identity_private_key.sign(message)).serialize());
   const auto extension_value = signed_key_der(forge::net::p2p::encode_public_key(identity_key), signature);

   auto object = asn1_object_ptr{OBJ_txt2obj("1.3.6.1.4.1.53594.1.1", 1), &ASN1_OBJECT_free};
   auto octets = asn1_octet_string_ptr{ASN1_OCTET_STRING_new(), &ASN1_OCTET_STRING_free};
   if (!object || !octets ||
       ASN1_OCTET_STRING_set(octets.get(), extension_value.data(), static_cast<int>(extension_value.size())) != 1) {
      throw std::runtime_error{"failed to create test libp2p identity extension"};
   }
   auto extension =
       x509_extension_ptr{X509_EXTENSION_create_by_OBJ(nullptr, object.get(), 1, octets.get()), &X509_EXTENSION_free};
   if (!extension || X509_add_ext(certificate.get(), extension.get(), -1) != 1 ||
       X509_sign(certificate.get(), key.get(), EVP_sha256()) <= 0) {
      throw std::runtime_error{"failed to sign test identity certificate"};
   }

   auto certificate_bio = bio_ptr{BIO_new(BIO_s_mem()), &BIO_free};
   if (!certificate_bio || PEM_write_bio_X509(certificate_bio.get(), certificate.get()) != 1) {
      throw std::runtime_error{"failed to encode test identity certificate"};
   }
   return identity_fixture{
       .certificate_pem = bio_to_string(certificate_bio.get()),
       .private_key_pem = std::move(private_key_pem),
   };
}

} // namespace forge::tests::p2p
