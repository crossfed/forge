#include <boost/test/unit_test.hpp>

#include <cstdint>
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

BOOST_AUTO_TEST_CASE(x509_fingerprint_owns_digest_while_copying_bytes) {
   const auto der = forge::crypto::core::bytes{
       std::uint8_t{0x30}, std::uint8_t{0x03}, std::uint8_t{0x02}, std::uint8_t{0x01}, std::uint8_t{0x01},
   };
   const auto expected = forge::crypto::digest::sha256::hash(der);
   const auto expected_bytes = expected.to_uint8_span();

   const auto actual = forge::crypto::pki::x509::certificate{der}.fingerprint_sha256();

   BOOST_REQUIRE_EQUAL(actual.size(), expected_bytes.size());
   BOOST_CHECK_EQUAL_COLLECTIONS(actual.begin(), actual.end(), expected_bytes.begin(), expected_bytes.end());
}

BOOST_AUTO_TEST_SUITE_END()
