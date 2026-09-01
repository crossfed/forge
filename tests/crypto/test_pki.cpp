#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <span>
#include <variant>

import forge.crypto.asymmetric;
import forge.crypto.core.types;
import forge.crypto.digest.sha256;
import forge.crypto.pki.der;
import forge.crypto.pki.x509;

BOOST_AUTO_TEST_SUITE(crypto_pki)

BOOST_AUTO_TEST_CASE(der_roundtrip_preserves_public_key) {
   const auto private_key = forge::crypto::asymmetric::private_key::generate_p256();
   const auto expected = private_key.get_public_key();
   const auto encoded = forge::crypto::pki::der::write_public_key(expected);
   const auto decoded = forge::crypto::pki::der::read_public_key(encoded);

   BOOST_CHECK(decoded == expected);
}

BOOST_AUTO_TEST_CASE(x509_fingerprint_is_standard_sha256_of_der_bytes) {
   const auto der = forge::crypto::core::bytes{
       std::uint8_t{0x30}, std::uint8_t{0x03}, std::uint8_t{0x02}, std::uint8_t{0x01}, std::uint8_t{0x01},
   };
   const auto expected = forge::crypto::digest::sha256::hash(std::span<const std::uint8_t>{der});
   const auto expected_bytes = expected.to_uint8_span();

   const auto certificate = forge::crypto::pki::x509::certificate{der};
   const auto actual = certificate.fingerprint_sha256();

   BOOST_REQUIRE_EQUAL(actual.size(), expected_bytes.size());
   BOOST_CHECK_EQUAL_COLLECTIONS(actual.begin(), actual.end(), expected_bytes.begin(), expected_bytes.end());
   BOOST_TEST(certificate.fingerprint_sha256_text() ==
              "1b65f68a522c858715f5dd951cd0402dc16691778814bf0759822b7a257421d0");
}

BOOST_AUTO_TEST_SUITE_END()
